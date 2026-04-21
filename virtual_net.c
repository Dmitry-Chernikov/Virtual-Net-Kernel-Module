#include "virtual_net.h"

#include <linux/etherdevice.h>
#include <linux/ip.h>
#include <linux/icmp.h>
#include <linux/proc_fs.h>
#include <linux/if_arp.h>
#include <linux/if_ether.h>
#include <linux/if.h>
#include <net/arp.h>

/**
 * @file virtual_net.c
 * @brief Виртуальный сетевой интерфейс ядра Linux с управлением IPv4 через sysctl/procfs.
 */


/**
 * @brief Инициализирует модуль: sysctl, интерфейс и стартовый IP.
 * @return 0 при успехе, отрицательный код ошибки при неудаче.
 */
static int __init virtual_net_init(void) {
    /* 1) Регистрируем sysctl-путь /proc/sys/net/vnet/ip_addr. */
    vnet_sysctl_header = register_sysctl("net/vnet", vnet_table);
    if (!vnet_sysctl_header) {
        pr_err("VNET: не удалось зарегистрировать sysctl-таблицу\n");
        return -ENOMEM;
    }

    /* 2) Создаём net_device с приватной областью struct vnet_priv. */
    vnet_dev = alloc_etherdev(sizeof(struct vnet_priv));
    if (!vnet_dev) {
        pr_err("VNET: не удалось выделить сетевое устройство\n");
        unregister_sysctl_table(vnet_sysctl_header);
        return -ENOMEM;
    }

    /* Базовая настройка интерфейса: имя, коллбэки, флаги, MAC. */
    strscpy(vnet_dev->name, VNET_IFNAME, IFNAMSIZ);
    vnet_dev->netdev_ops = &vnet_ops;
    vnet_dev->flags |= IFF_NOARP;
    eth_hw_addr_random(vnet_dev);

    /* MTU интерфейса (максимальный payload кадра без фрагментации на L3). */
    vnet_dev->mtu = 1500;

    /* 3) Регистрируем устройство в сетевом стеке ядра. */
    int err = register_netdev(vnet_dev);
    if (err) {
        pr_err("VNET: не удалось зарегистрировать сетевое устройство\n");
        free_netdev(vnet_dev);
        unregister_sysctl_table(vnet_sysctl_header);
        return err;
    }

    pr_info("VNET: модуль загружен: IP=%s, MAC=%pM\n", vnet_ip_str, vnet_dev->dev_addr);

    /* 4) Переводим интерфейс в состояние UP под RTNL lock. */
    rtnl_lock();
        err = dev_change_flags(vnet_dev, vnet_dev->flags | IFF_UP, NULL);
    rtnl_unlock();

    if (err < 0) {
        pr_err("VNET: не удалось поднять интерфейс %s (ошибка %d)\n", vnet_dev->name, err);
        pr_info("VNET: можно поднять интерфейс вручную: sudo ip link set %s up\n", vnet_dev->name);
        unregister_netdev(vnet_dev);
        free_netdev(vnet_dev);
        unregister_sysctl_table(vnet_sysctl_header);
        return err;
    }
    pr_info("VNET: интерфейс %s переведён в состояние UP\n", vnet_dev->name);

    /* 5) Назначаем стартовый IPv4 через /sbin/ip (userspace helper). */
    err = modify_vif_ip_userspace(VNET_IFNAME, vnet_ip_str, true);
    if (err) {
        pr_err("VNET: не удалось назначить IP %s на %s (ошибка %d)\n", vnet_ip_str, vnet_dev->name, err);
        pr_warn("VNET: можно назначить IP вручную: sudo ip addr add %s/24 dev %s\n", vnet_ip_str, vnet_dev->name);
    } else {
        pr_info("VNET: IP %s/24 назначен интерфейсу %s\n", vnet_ip_str, vnet_dev->name);
    }

    pr_info("VNET: модуль успешно инициализирован, MAC=%pM\n", vnet_dev->dev_addr);
    pr_info("VNET: изменить IP можно так: echo \"NEW_IP\" | sudo tee /proc/sys/net/vnet/ip_addr\n");
    return 0;
}

/** @brief Освобождает ресурсы модуля при выгрузке. */
static void __exit virtual_net_exit(void) {
    if (vnet_dev) {
        pr_info("VNET: выгрузка интерфейса: IP=%s, MAC=%pM\n", vnet_ip_str, vnet_dev->dev_addr);
        unregister_netdev(vnet_dev);
        free_netdev(vnet_dev);
    }
    if (vnet_sysctl_header) {
        pr_info("VNET: освобождение sysctl-таблицы\n");
        unregister_sysctl_table(vnet_sysctl_header);
        vnet_sysctl_header = NULL;
    }

    pr_info("VNET: модуль выгружен\n");
}

/**
 * @brief Преобразует IPv4 строку в 32-битный адрес в network byte order.
 * @param ip_out Указатель на выходное значение IPv4 (network byte order).
 * @param ip_str Строковый IPv4 адрес.
 * @param lock Spinlock для синхронизации чтения строки адреса.
 * @return true, если адрес корректен и успешно распознан; иначе false.
 */
static bool str_to_ip(__u32 *ip_out, const char *ip_str, spinlock_t *lock) {
    /* Защищаем общий буфер адреса от гонок при чтении/записи. */
    spin_lock_bh(lock);
    if (in4_pton(ip_str, -1, (u8 *) ip_out, -1, NULL) <= 0) {
        spin_unlock_bh(lock);
        return false;
    }
    spin_unlock_bh(lock);
    return true;
}

/**
 * @brief Обрабатывает skb интерфейса и эмулирует ответное сетевое поведение.
 * @param skb Буфер пакета.
 * @param dev Сетевое устройство.
 * @return NETDEV_TX_OK во всех ветках обработки.
 *
 * Поддерживает:
 * - ARP request -> ARP reply для текущего адреса `vnet_ip_str`.
 * - ICMP Echo Request -> ICMP Echo Reply.
 */
static netdev_tx_t vnet_xmit(struct sk_buff *skb, struct net_device *dev) {
    struct vnet_priv *priv = netdev_priv(dev);
    __u32 target_ip = 0;

    /* Базовая защита от некорректных аргументов драйверного коллбэка. */
    if (!skb || !dev) {
        if (skb)
            dev_kfree_skb(skb);
        return NETDEV_TX_OK;
    }

    /* Получаем текущий адрес интерфейса из sysctl-буфера в бинарном формате. */
    if (!str_to_ip(&target_ip, vnet_ip_str, &vnet_ip_lock)) {
        priv->stats.tx_dropped++;
        dev_kfree_skb(skb);
        return NETDEV_TX_OK;
    }

    /* ARP кадры обрабатываем отдельно: отвечаем на запросы к своему IP. */
    if (skb->protocol == htons(ETH_P_ARP)) {
        vnet_arp_reply(skb, dev, target_ip);
        priv->stats.rx_packets++;
        priv->stats.rx_bytes += skb->len;
        dev_kfree_skb(skb);
        return NETDEV_TX_OK;
    }

    /* Для IPv4 валидируем заголовок, checksum и обрабатываем только ICMP Echo. */
    if (skb->protocol == htons(ETH_P_IP)) {
        /* Синхронизируем network_header, чтобы ip_hdr() указывал на корректные данные. */
        skb_reset_network_header(skb);

        struct iphdr *iph = ip_hdr(skb);

        /* Защита от короткого (битого) пакета: заголовок не помещается в skb. */
        if (unlikely(skb->len < sizeof(struct iphdr))) {
            priv->stats.rx_length_errors++;
            dev_kfree_skb(skb);
            return NETDEV_TX_OK;
        }

        /* Проверяем checksum IPv4-заголовка до дальнейшей обработки. */
        if (ip_fast_csum((u8 *) iph, iph->ihl) != 0) {
            priv->stats.rx_crc_errors++;
            dev_kfree_skb(skb);
            return NETDEV_TX_OK;
        }

        /* Интересуют только ICMP Echo Request в адрес текущего IP интерфейса. */
        if (iph->protocol == IPPROTO_ICMP) {
            /* Убеждаемся, что в пакете есть весь ICMP-заголовок. */
            if (unlikely(skb->len < iph->ihl * 4 + sizeof(struct icmphdr))) {
                priv->stats.rx_length_errors++;
                dev_kfree_skb(skb);
                return NETDEV_TX_OK;
            }

            /* Устанавливаем транспортный заголовок для корректного icmp_hdr(). */
            skb_set_transport_header(skb, skb_network_offset(skb) + (int) skb_network_header_len(skb));

            const struct icmphdr *icmph = icmp_hdr(skb);

            /* Echo request на наш адрес: формируем копию и превращаем её в reply. */
            if (icmph->type == ICMP_ECHO && iph->daddr == target_ip) {
                struct sk_buff *new_skb = skb_copy(skb, GFP_ATOMIC);

                if (new_skb) {
                    struct iphdr *new_iph = ip_hdr(new_skb);
                    struct icmphdr *new_icmph = icmp_hdr(new_skb);

                    /* Меняем source/destination местами для ответа. */
                    const __u32 tmp = new_iph->saddr;
                    new_iph->saddr = new_iph->daddr;
                    new_iph->daddr = tmp;

                    /* После изменения заголовка обязательно пересчитываем IPv4 checksum. */
                    new_iph->check = 0;
                    new_iph->check = ip_fast_csum((u8 *) new_iph, new_iph->ihl);

                    /* Тип ICMP меняем с Echo Request на Echo Reply. */
                    new_icmph->type = ICMP_ECHOREPLY;

                    /* Пересчитываем ICMP checksum уже для модифицированного пакета. */
                    new_icmph->checksum = 0;
                    new_icmph->checksum = ip_compute_csum((unsigned char *) new_icmph,
                                                          ntohs(new_iph->tot_len) - new_iph->ihl * 4);

                    /* Возвращаем пакет в сетевой стек, как будто он пришёл от интерфейса. */
                    new_skb->dev = dev;
                    new_skb->protocol = eth_type_trans(new_skb, dev);

                    /* tx_* считаем для сформированного ответа, rx_* — для принятого стеком. */
                    priv->stats.tx_packets++;
                    priv->stats.tx_bytes += new_skb->len;

                    if (netif_rx(new_skb) == NET_RX_SUCCESS) {
                        priv->stats.rx_packets++;
                        priv->stats.rx_bytes += new_skb->len;
                    }

                    /* Исходный skb уже не нужен: ответ сформирован на копии. */
                    dev_kfree_skb(skb);
                    return NETDEV_TX_OK;
                }

                /* Не удалось выделить буфер под ответ: учитываем как tx ошибку. */
                priv->stats.tx_errors++;
                dev_kfree_skb(skb);
                return NETDEV_TX_OK;
            }
        }
    }

    /* Все неподдержанные кадры/протоколы дропаем как simulated NIC. */
    priv->stats.tx_dropped++;
    dev_kfree_skb(skb);
    return NETDEV_TX_OK;
}

/**
 * @brief Возвращает статистику интерфейса для ndo_get_stats64.
 * @param dev Сетевое устройство.
 * @param stats Буфер назначения статистики.
 */
static void vnet_get_stats64(struct net_device *dev, struct rtnl_link_stats64 *stats) {
    struct vnet_priv const *priv = netdev_priv(dev);
    memcpy(stats, &priv->stats, sizeof(*stats));
}

/**
 * @brief Формирует и отправляет ARP reply.
 * @param skb Входящий ARP request.
 * @param dev Сетевое устройство.
 * @param target_ip IPv4 адрес интерфейса в сетевом порядке.
 */
static void vnet_arp_reply(struct sk_buff const *skb, struct net_device *dev, __u32 target_ip) {
    struct arphdr *arp;
    unsigned char const *arp_ptr;
    unsigned char const *sha;
    unsigned char const *tha;
    __u32 sip;
    __u32 tip;
    struct sk_buff *reply_skb;

    arp = arp_hdr(skb);

    if (arp->ar_op != htons(ARPOP_REQUEST))
        return;

    /* Из ARP payload извлекаем MAC/IP отправителя и целевой IP запроса. */
    arp_ptr = (unsigned char *) (arp + 1);
    sha = arp_ptr;
    arp_ptr += dev->addr_len;
    memcpy(&sip, arp_ptr, 4);
    arp_ptr += 4;
    tha = arp_ptr;
    arp_ptr += dev->addr_len;
    memcpy(&tip, arp_ptr, 4);

    /* Отвечаем только на ARP-запросы к текущему адресу интерфейса. */
    if (tip != target_ip)
        return;

    /* Конструируем и отправляем ARP reply с MAC нашего интерфейса. */
    reply_skb = arp_create(ARPOP_REPLY, ETH_P_ARP, sip, dev, tip, sha, dev->dev_addr, sha);

    if (!reply_skb) {
        pr_err("VNET: не удалось сформировать ARP reply\n");
        return;
    }

    arp_xmit(reply_skb);

    struct vnet_priv *priv = netdev_priv(dev);
    priv->stats.tx_packets++;
    priv->stats.tx_bytes += skb->len;
}

/**
 * @brief Обработчик sysctl для /proc/sys/net/vnet/ip_addr.
 * @param table Описание параметра sysctl.
 * @param write Флаг операции записи.
 * @param buffer Буфер данных от VFS/sysctl.
 * @param lenp Размер буфера.
 * @param ppos Позиция в буфере.
 * @return 0 при успехе или отрицательный код ошибки.
 */
static int vnet_sysctl_handler(const struct ctl_table *table, int write, void *buffer, size_t *lenp, loff_t *ppos) {
    int ret = 0;
    char old_ip[16];

    /* Сохраняем старый IP, чтобы можно было откатить значение при ошибке. */
    strcpy(old_ip, vnet_ip_str);

    ret = proc_dostring(table, write, buffer, lenp, ppos);
    if (ret) {
        pr_err("VNET: ошибка обработки sysctl-запроса\n");
        return ret;
    }

    /* Для операции записи: валидируем новый адрес и применяем его к интерфейсу. */
    if (write && vnet_dev) {
        if (strcmp(old_ip, vnet_ip_str) != 0) {
            __be32 test_ip;

            /* Сначала проверяем формат нового IPv4 адреса. */
            if (!str_to_ip(&test_ip, vnet_ip_str, &vnet_ip_lock)) {
                pr_err("VNET: неверный формат IP-адреса: %s\n", vnet_ip_str);
                strcpy(vnet_ip_str, old_ip);
                return -EINVAL;
            }

            pr_info("VNET: IP изменён: %s -> %s\n", old_ip, vnet_ip_str);

            /* Порядок обновления: удалить старый адрес, затем добавить новый. */
            ret = modify_vif_ip_userspace(VNET_IFNAME, old_ip, false);
            if (ret) {
                pr_err("VNET: не удалось удалить старый IP-адрес %s (ошибка %d)\n", old_ip, ret);
                strcpy(vnet_ip_str, old_ip);
                return ret;
            }
            pr_info("VNET: старый IP-адрес удалён: %s\n", old_ip);

            /* После успешного удаления назначаем новый адрес. */
            ret = modify_vif_ip_userspace(VNET_IFNAME, vnet_ip_str, true);
            if (ret) {
                pr_err("VNET: не удалось добавить новый IP-адрес %s (ошибка %d)\n", vnet_ip_str, ret);
                strcpy(vnet_ip_str, old_ip);
                return ret;
            }

            pr_info("VNET: IP-адрес интерфейса обновлён до %s\n", vnet_ip_str);
        }
    }

    return 0;
}

/**
 * @brief Изменяет IPv4 адрес интерфейса через userspace-команду `ip`.
 * @param name Имя сетевого интерфейса.
 * @param ip_str IPv4 адрес в строковом виде.
 * @param is_add true для add, false для del.
 * @return Код возврата call_usermodehelper().
 */
static int modify_vif_ip_userspace(const char *name, const char *ip_str, bool is_add) {
    char *argv[8];
    char cmd[256];
    int i = 0;

    if (!name || !ip_str)
        return -EINVAL;

    /* Формируем shell-команду для add/del адреса на интерфейсе. */
    if (is_add) {
        snprintf(cmd, sizeof(cmd), "/sbin/ip addr add %s/24 dev %s 2>/dev/null", ip_str, name);
    } else {
        snprintf(cmd, sizeof(cmd), "/sbin/ip addr del %s/24 dev %s 2>/dev/null", ip_str, name);
    }

    argv[i++] = "/bin/sh";
    argv[i++] = "-c";
    argv[i++] = cmd;
    argv[i] = NULL;

    pr_info("VNET: выполнение команды: %s\n", cmd);

    /* call_usermodehelper запускает /bin/sh -c "<ip command>" и ждёт завершения. */
    return call_usermodehelper(argv[0], argv, NULL, UMH_WAIT_PROC);
}

module_init(virtual_net_init);
module_exit(virtual_net_exit);

MODULE_LICENSE("MIT");
MODULE_AUTHOR("Dmitry Chernikov");
MODULE_DESCRIPTION("Virtual Net Interface");
