
# this Makefile is not intended to go on the sliver image - esp. not in /usr/sbin
#	planetlab/scripts/Makefile
# same goes for showgraph
#	planetlab/scripts/showgraph
dist_sbin_SCRIPTS += planetlab/scripts/sliver-ovs 

EXTRA_DIST += \
	planetlab/scripts/sliver-ovs.in
