# Copyright (c) 2020 PHYTEC Messtechnik GmbH
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_BCBUS)
	zephyr_library()

	zephyr_include_directories(${ZEPHYR_BASE}/subsys/bcbus)

	zephyr_library_sources(
		bcbus_core.c
		bcbus_unit.c
	)

	zephyr_library_sources_ifdef(
		CONFIG_BCBUS_SERIAL
		bcbus_serial.c
	)

	zephyr_library_sources_ifdef(
		CONFIG_BCBUS_RAW_ADU
		bcbus_raw.c
	)

	zephyr_library_sources_ifdef(
		CONFIG_BCBUS_SERVER
		bcbus_server.c
	)

	zephyr_library_sources_ifdef(
		CONFIG_BCBUS_CLIENT
		bcbus_client.c
		nd6520_proto.c
	)
endif()
