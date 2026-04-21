#MY_C ?= x86_64-linux-gnu-gcc-13

obj-m += vnet.o
vnet-objs := virtual_net.o

# Добавляем флаги для отладки
# -O2 уровень оптимизации
# -g вывод отладочной информации
# -DDEBUG макрос DDEBUG добавляет дополнительный код в заголовок ядра
ccflags-y := -Wall
#ccflags-y := -Wall -O2 -g

SHELL := $(shell which bash)
KDIR ?= /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

.PHONY: help init all clean compile_commands docs docs-serve install uninstall load unload logs test status info

help:
	@printf "\n"
	@printf "═══════════════════════════════════════════════════════════════════\n"
	@printf "  VIRTUAL NETWORK INTERFACE MODULE - MAKE COMMANDS\n"
	@printf "═══════════════════════════════════════════════════════════════════\n"
	@printf "\n"
	@printf "📦 ОСНОВНЫЕ КОМАНДЫ:\n"
	@printf "  make all      - Скомпилировать модуль\n"
	@printf "  make compile_commands - Сгенерировать compile_commands.json для IDE (bear)\n"
	@printf "  make docs     - Сгенерировать документацию Doxygen в ./docs/html\n"
	@printf "  make docs-serve - Запустить локальный HTTP сервер документации на :8000\n"
	@printf "  make clean    - Очистить собранные файлы\n"
	@printf "  make load     - Загрузить модуль в ядро (требует sudo)\n"
	@printf "  make unload   - Выгрузить модуль из ядра (требует sudo)\n"
	@printf "  make reload   - Перезагрузить модуль (выгрузить + загрузить)\n"
	@printf "\n"
	@printf "🔧 РАЗРАБОТКА И ОТЛАДКА:\n"
	@printf "  make test     - Скомпилировать, загрузить и настроить интерфейс\n"
	@printf "  make logs     - Показать последние сообщения ядра (dmesg)\n"
	@printf "  make status   - Показать статус модуля и интерфейса\n"
	@printf "  make info     - Показать информацию о модуле\n"
	@printf "  make watch    - Мониторить логи ядра в реальном времени\n"
	@printf "\n"
	@printf "⚙️  УСТАНОВКА В СИСТЕМУ:\n"
	@printf "  make install   - Установить модуль в систему (требует sudo)\n"
	@printf "  make uninstall - Удалить модуль из системы (требует sudo)\n"
	@printf "\n"
	@printf "🖥️  НАСТРОЙКА ОКРУЖЕНИЯ:\n"
	@printf "  make init      - Установить зависимости для сборки (Debian/RHEL)\n"
	@printf "\n"
	@printf "📖 ПРИМЕРЫ ИСПОЛЬЗОВАНИЯ:\n"
	@printf "  # Быстрый старт для тестирования\n"
	@printf "    make test\n"
	@printf "    ping -c 4 10.0.0.1\n"
	@printf "    make unload\n"
	@printf "\n"
	@printf "  # Разработка и отладка\n"
	@printf "    make all                 # Собрать модуль\n"
	@printf "    sudo insmod vnet.ko      # Загрузить вручную\n"
	@printf "    make logs                # Посмотреть логи\n"
	@printf "    sudo rmmod vnet          # Выгрузить\n"
	@printf "\n"
	@printf "  # Постоянная установка в систему\n"
	@printf "    make install             # Установить модуль\n"
	@printf "    sudo modprobe vnet       # Загрузить по имени\n"
	@printf "    echo 'vnet' | sudo tee -a /etc/modules  # Автозагрузка\n"
	@printf "\n"
	@printf "  # Изменение IP адреса\n"
	@printf "    echo '10.0.0.100' | sudo tee /proc/sys/net/vnet/ip_addr\n"
	@printf "    cat /proc/sys/net/vnet/ip_addr\n"
	@printf "\n"
	@printf "🔍 ДИАГНОСТИКА:\n"
	@printf "  # Проверка что модуль загружен\n"
	@printf "    lsmod | grep vnet\n"
	@printf "\n"
	@printf "  # Проверка сетевого интерфейса\n"
	@printf "    ip link show vnet0\n"
	@printf "    ip addr show vnet0\n"
	@printf "    ip -s link show vnet0\n"
	@printf "\n"
	@printf "  # Просмотр логов в реальном времени\n"
	@printf "    sudo journalctl -f -k | grep VNET\n"
	@printf "    # или\n"
	@printf "    make watch\n"
	@printf "\n"
	@printf "⚠️  ВОЗМОЖНЫЕ ПРОБЛЕМЫ И РЕШЕНИЯ:\n"
	@printf "  Проблема: 'Module vnet already loaded'\n"
	@printf "    Решение: make unload\n"
	@printf "\n"
	@printf "  Проблема: 'vnet.ko not found'\n"
	@printf "    Решение: make all\n"
	@printf "\n"
	@printf "  Проблема: 'Operation not permitted'\n"
	@printf "    Решение: Используйте sudo или запускайте от root\n"
	@printf "\n"
	@printf "  Проблема: Ping не работает\n"
	@printf "    Решение: \n"
	@printf "      sudo ip link set vnet0 up\n"
	@printf "      sudo ip addr add 10.0.0.1/24 dev vnet0\n"
	@printf "      cat /proc/sys/net/vnet/ip_addr  # Проверьте IP\n"
	@printf "\n"
	@printf "═══════════════════════════════════════════════════════════════════\n"

init:
	@set -euo pipefail; \
	echo "Обнаружение дистрибутива/менеджера пакетов..."; \
	if command -v apt-get >/dev/null 2>&1; then \
		echo "Использование apt (Debian/Ubuntu)"; \
		sudo apt-get update; \
		sudo apt-get install -y --no-install-recommends \
			build-essential \
			dkms \
			kmod \
			bc \
			bison \
			flex \
			libelf-dev \
			libssl-dev \
			dwarves \
			"linux-headers-$$(uname -r)"; \
	elif command -v dnf >/dev/null 2>&1; then \
		echo "Использование dnf (семейство RHEL/Fedora)"; \
		sudo dnf -y install \
			gcc \
			make \
			dkms \
			kmod \
			bc \
			bison \
			flex \
			elfutils-libelf-devel \
			openssl-devel \
			dwarves \
			"kernel-devel-$$(uname -r)" \
			"kernel-headers-$$(uname -r)" || \
		sudo dnf -y install \
			gcc \
			make \
			dkms \
			kmod \
			bc \
			bison \
			flex \
			elfutils-libelf-devel \
			openssl-devel \
			dwarves \
			kernel-devel \
			kernel-headers; \
	elif command -v yum >/dev/null 2>&1; then \
		echo "Использование yum (семейство RHEL/CentOS)"; \
		sudo yum -y install \
			gcc \
			make \
			dkms \
			kmod \
			bc \
			bison \
			flex \
			elfutils-libelf-devel \
			openssl-devel \
			dwarves \
			"kernel-devel-$$(uname -r)" \
			"kernel-headers-$$(uname -r)" || \
		sudo yum -y install \
			gcc \
			make \
			dkms \
			kmod \
			bc \
			bison \
			flex \
			elfutils-libelf-devel \
			openssl-devel \
			dwarves \
			kernel-devel \
			kernel-headers; \
	else \
		echo "ОШИБКА: не найдены поддерживаемые менеджеры пакетов (apt-get/dnf/yum)."; \
		echo "Этот Makefile поддерживает системы на базе Debian и RHEL."; \
		exit 1; \
	fi; \
	echo "Done."

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

# compile_commands.json для Cursor/VS Code (C/C++, clangd). Нужен пакет bear.
compile_commands:
	@command -v bear >/dev/null 2>&1 || { printf '%s\n' "Установите bear, например: sudo apt install bear"; exit 1; }
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	bear -- $(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	@rm -rf docs

# Генерация документации Doxygen (требуется файл Doxyfile в корне проекта)
docs:
	@command -v doxygen >/dev/null 2>&1 || { printf '%s\n' "Установите doxygen, например: sudo apt install doxygen"; exit 1; }
	@if [ ! -f Doxyfile ]; then \
		echo "❌ Файл Doxyfile не найден в корне проекта"; \
		exit 1; \
	fi
	doxygen Doxyfile
	@echo "✅ Документация сгенерирована: ./docs/html/index.html"

# Локальный веб-сервер для просмотра документации
docs-serve:
	@if [ ! -d ./docs/html ]; then \
		echo "❌ Каталог ./docs/html не найден. Сначала выполните: make docs"; \
		exit 1; \
	fi
	@echo "🌐 Документация доступна по адресу: http://127.0.0.1:8000/"
	python3 -m http.server 8000 --directory ./docs/html/

# Устанавливает модуль в системное дерево модулей
install: all
	sudo $(MAKE) -C $(KDIR) M=$(PWD) modules_install
	sudo depmod -a
	@echo "✅ Модуль установлен в систему"
	@echo "   Загрузка: sudo modprobe vnet"
	@echo "   Выгрузка: sudo modprobe -r vnet"

# Удаляет модуль из системы
uninstall:
	@sudo rm -f /lib/modules/$(shell uname -r)/extra/vnet.ko
	@sudo rm -f /lib/modules/$(shell uname -r)/misc/vnet.ko
	@sudo depmod -a
	@echo "✅ Модуль удален из системы"

# Загружает модуль в работающее ядро
load: all
	@if lsmod | grep -q vnet; then \
		echo "⚠️  Модуль vnet уже загружен"; \
		echo "   Выполните 'make unload' сначала или 'make reload'"; \
		exit 1; \
	fi
	@if [ ! -f vnet.ko ]; then \
		echo "❌ Файл vnet.ko не найден"; \
		echo "   Выполните 'make all' для компиляции"; \
		exit 1; \
	fi
	@if ! file vnet.ko | grep -q "ELF 64-bit"; then \
		echo "❌ Файл vnet.ko не является валидным модулем ядра"; \
		echo "   Выполните 'make clean && make all' для перекомпиляции"; \
		exit 1; \
	fi
	sudo insmod vnet.ko
	@echo "✅ Модуль vnet загружен"
	@echo "   IP адрес: $$(cat /proc/sys/net/vnet/ip_addr 2>/dev/null || echo 'не установлен')"
	@echo ""
	@echo "📌 Для настройки интерфейса выполните:"
	@echo "   sudo ip link set vnet0 up"
	@echo "   sudo ip addr add 10.0.0.1/24 dev vnet0"

# Выгружает модуль из ядра
unload:
	@if ! lsmod | grep vnet; then \
		echo "⚠️  Модуль vnet не загружен"; \
		exit 1; \
	fi
	-sudo ip link set vnet0 down 2>/dev/null
	-sudo ip addr flush dev vnet0 2>/dev/null
	sudo rmmod vnet
	@echo "✅ Модуль vnet выгружен"

# Перезагружает модульmo
reload: unload load
	@echo "✅ Модуль перезагружен"

# Просмотр логов
logs:
	@echo "📋 Последние 20 сообщений ядра:"
	@echo "═══════════════════════════════════════════════════════════════════"
	sudo dmesg | tail -20 | grep --color=always -E "VNET|$$"

# Мониторинг логов в реальном времени
watch:
	@echo "📡 Мониторинг логов ядра (Ctrl+C для выхода)..."
	sudo dmesg -w | grep --color=always -E "VNET|$$"

# Статус модуля и интерфейса
status:
	@echo "═══════════════════════════════════════════════════════════════════"
	@echo "  СТАТУС МОДУЛЯ VNET"
	@echo "═══════════════════════════════════════════════════════════════════"
	@echo ""
	@if lsmod | grep -q vnet; then \
		echo "✅ Модуль: ЗАГРУЖЕН"; \
		echo ""; \
		echo "📊 Информация о модуле:"; \
		lsmod | grep vnet; \
	else \
		echo "❌ Модуль: НЕ ЗАГРУЖЕН"; \
	fi
	@echo ""
	@if ip link show vnet0 2>/dev/null | grep -q "state"; then \
		echo "🌐 Интерфейс vnet0: СУЩЕСТВУЕТ"; \
		echo ""; \
		ip link show vnet0; \
		echo ""; \
		ip addr show vnet0 2>/dev/null || echo "  IP адрес не назначен"; \
		echo ""; \
		ip -s link show vnet0 2>/dev/null | grep -A 5 "RX:\|TX:"; \
	else \
		echo "🌐 Интерфейс vnet0: НЕ СУЩЕСТВУЕТ"; \
	fi
	@echo ""
	@if [ -f /proc/sys/net/vnet/ip_addr ]; then \
		echo "🔧 Sysctl настройки:"; \
		echo "  IP адрес: $$(cat /proc/sys/net/vnet/ip_addr)"; \
	else \
		echo "⚠️  Sysctl интерфейс не найден (модуль не загружен)"; \
	fi
	@echo "═══════════════════════════════════════════════════════════════════"

# Информация о модуле
info:
	@if [ -f vnet.ko ]; then \
		echo "═══════════════════════════════════════════════════════════════════"; \
		echo "  ИНФОРМАЦИЯ О МОДУЛЕ VNET"; \
		echo "═══════════════════════════════════════════════════════════════════"; \
		echo ""; \
		modinfo vnet.ko; \
	else \
		echo "❌ Модуль не скомпилирован. Выполните 'make all'"; \
	fi

# Быстрый тест (компиляция + загрузка + настройка)
test: all load
	@echo ""
	@echo "🔧 Настройка сетевого интерфейса..."
	-sudo ip link set vnet0 up 2>/dev/null
	-sudo ip addr add 10.0.0.1/24 dev vnet0 2>/dev/null
	@echo ""
	@echo "═══════════════════════════════════════════════════════════════════"
	@echo "  ✅ ГОТОВО К ТЕСТИРОВАНИЮ!"
	@echo "═══════════════════════════════════════════════════════════════════"
	@echo ""
	@echo "📝 Проверка работы:"
	@echo "   ping -c 4 10.0.0.1"
	@echo ""
	@echo "📊 Просмотр статистики:"
	@echo "   make status"
	@echo ""
	@echo "📋 Просмотр логов:"
	@echo "   make logs"
	@echo ""
	@echo "🔄 Для выгрузки модуля:"
	@echo "   make unload"
	@echo "═══════════════════════════════════════════════════════════════════"
	@echo ""
	@ping -c 2 10.0.0.1 2>/dev/null && echo "✅ PING УСПЕШЕН!" || echo "⚠️  PING НЕ РАБОТАЕТ (проверьте настройки)"