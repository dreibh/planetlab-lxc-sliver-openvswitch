sbin_PROGRAMS += planetlab/pltap-ovs/pltap-ovs 
sbin_PROGRAMS += planetlab/vsysc/vsysc

# this Makefile is not intended to go on the sliver image - esp. not in /usr/sbin
#	planetlab/scripts/Makefile
# same goes for showgraph
#	planetlab/scripts/showgraph
dist_sbin_SCRIPTS += planetlab/scripts/sliver-ovs 

planetlab_pltap_ovs_pltap_ovs_SOURCES =
planetlab_pltap_ovs_pltap_ovs_SOURCES += planetlab/pltap-ovs/pltap-ovs.c
planetlab_pltap_ovs_pltap_ovs_SOURCES += planetlab/pltap-ovs/tunalloc.c
planetlab_pltap_ovs_pltap_ovs_SOURCES += planetlab/pltap-ovs/tunalloc.h

planetlab_vsysc_vsysc_SOURCES =
planetlab_vsysc_vsysc_SOURCES += planetlab/vsysc/vsysc.c

EXTRA_DIST += \
	planetlab/scripts/sliver-ovs.in
