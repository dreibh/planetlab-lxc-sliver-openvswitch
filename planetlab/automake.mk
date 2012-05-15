sbin_PROGRAMS += \
	planetlab/pltap-ovs/pltap-ovs \
	planetlab/vsysc/vsysc

dist_sbin_SCRIPTS += \
	planetlab/scripts/start_ovsdb-server \
	planetlab/scripts/start_vswitchd \
	planetlab/scripts/create_bridge \
	planetlab/scripts/create_port \
	planetlab/scripts/del_bridge \
	planetlab/scripts/del_port \
	planetlab/scripts/Makefile

planetlab_pltap_ovs_pltap_ovs_SOURCES = \
	planetlab/pltap-ovs/pltap-ovs.c \
	planetlab/pltap-ovs/tunalloc.c \
	planetlab/pltap-ovs/tunalloc.h

planetlab_vsysc_vsysc_SOURCES = \
	planetlab/vsysc/vsysc.c
