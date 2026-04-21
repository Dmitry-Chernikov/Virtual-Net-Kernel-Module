#pragma once

/**
 * @file virtual_net.h
 * @brief Общие определения для модуля виртуального сетевого интерфейса.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/inet.h>
#include <linux/sysctl.h>
#include <linux/rtnetlink.h>
#include <linux/types.h>
#include <net/ip.h>

/** @brief Имя создаваемого виртуального интерфейса. */
#define VNET_IFNAME "vnet0"

/** @brief IPv4 адрес интерфейса по умолчанию. */
#define VNET_DEFAULT_IP "10.0.0.1"

/** @brief Текущее значение IPv4 адреса интерфейса в строковом виде. */
static char vnet_ip_str[16] = VNET_DEFAULT_IP; /**< Текущий IPv4 адрес интерфейса в строковом виде. */
/** @brief Spinlock для синхронизации чтения/записи vnet_ip_str. */
static DEFINE_SPINLOCK(vnet_ip_lock); /**< Блокировка доступа к строке IP адреса. */
/** @brief Дескриптор Зарегистрированной таблицы sysctl. */
static struct ctl_table_header *vnet_sysctl_header; /**< Хендлер таблицы /proc/sys/net/vnet. */
/** @brief Указатель на зарегистрированное сетевое устройство vnet. */
static struct net_device *vnet_dev; /**< Указатель на net_device виртуального интерфейса. */

/** @brief Инициализация и регистрация модуля в ядре. */
static int __init virtual_net_init(void);

/** @brief Выгрузка и очистка ресурсов модуля. */
static void __exit virtual_net_exit(void);

/** @brief Обработчик передачи пакета с эмуляцией ARP/ICMP ответа. */
static netdev_tx_t vnet_xmit(struct sk_buff *skb, struct net_device *dev);

/** @brief Возвращает статистику интерфейса. */
static void vnet_get_stats64(struct net_device *dev, struct rtnl_link_stats64 *stats);

/** @brief Формирует и отправляет ARP reply для целевого IP. */
static void vnet_arp_reply(struct sk_buff const *skb, struct net_device *dev, __u32 target_ip);

/** @brief Обработчик записи/чтения /proc/sys/net/vnet/ip_addr. */
static int vnet_sysctl_handler(const struct ctl_table *table, int write, void *buffer, size_t *lenp, loff_t *ppos);

/** @brief Преобразует IPv4 строку в сетевой формат. */
static bool str_to_ip(__u32 *ip_out, const char *ip_str, spinlock_t *lock);

/** @brief Изменяет IPv4 интерфейса через userspace-утилиту `ip`. */
static int modify_vif_ip_userspace(const char *name, const char *ip_str, bool is_add);

/** @brief Приватные данные интерфейса vnet. */
struct vnet_priv {
    struct rtnl_link_stats64 stats; /**< Статистика RX/TX пакетов, байтов и ошибок. */
};

/** @brief Операции сетевого устройства vnet. */
static const struct net_device_ops vnet_ops = {
    .ndo_start_xmit = vnet_xmit,      /**< Коллбэк передачи пакета. */
    .ndo_get_stats64 = vnet_get_stats64, /**< Коллбэк выдачи статистики интерфейса. */
};

/** @brief Таблица sysctl для /proc/sys/net/vnet/ip_addr. */
static struct ctl_table vnet_table[] = {
    {
        .procname = "ip_addr",             /**< Имя параметра в /proc/sys/net/vnet/. */
        .data = vnet_ip_str,               /**< Буфер строкового IPv4 адреса. */
        .maxlen = sizeof(vnet_ip_str),     /**< Максимальный размер буфера адреса. */
        .mode = 0644,                      /**< Права доступа к sysctl-параметру. */
        .proc_handler = vnet_sysctl_handler, /**< Обработчик чтения/записи sysctl. */
    },
};



