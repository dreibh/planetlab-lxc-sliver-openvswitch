sbin_PROGRAMS += \
	planetlab/pltap-ovs/pltap-ovs

dist_sbin_SCRIPTS += \
	planetlab/scripts/create_bridge \
	planetlab/scripts/create_port \
	planetlab/scripts/del_bridge

planetlab_pltap_ovs_pltap_ovs_SOURCES = \
	planetlab/pltap-ovs/pltap-ovs.c \
	planetlab/pltap-ovs/tunalloc.c \
	planetlab/pltap-ovs/tunalloc.h
