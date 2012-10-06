# see README
# conf.mk is expected to define
# HOST_<id> and IP_<id> for all nodes involved, as well as 
# LINKS as a list of <node_id>-<node_id> elements

# run make CONF=anotherconfig.mk if you need several configs

CONF ?= conf.mk
include $(CONF)

# if undefined in the conf file, use single dash
SEP?=-

# bridge name (XXX the same on all nodes)
BRIDGE?=$(SLICE)

### helper functions
# flip(1) = 2
# flip(2) = 1
flip=$(if $(findstring 1,$(1)),2,1)
# cutsep (x-y)-> x y
cutsep=$(subst $(SEP), ,$(1))
# leftnode (x-y) -> x
leftnode=$(word 1,$(call cutsep,$(1)))
# rightnode (x-y) -> y
rightnode=$(word 2,$(call cutsep,$(1)))
# linkpart(x@y) = x
linkpart=$(word 1,$(subst @, ,$(1)))
# endpart(x@y) = y
endpart=$(word 2,$(subst @, ,$(1)))
# get(x-y@1) = x
# get(x-y@2) = y
get=$(word $(call endpart,$(1)),$(call cutsep,$(call linkpart,$(1))))
# opp(x-y@1) = x-y@2
# opp(x-y@2) = x-y@1
opp=$(call linkpart,$(1))@$(call flip,$(call endpart,$(1)))
# rget(x-y@1) = y
# rget(x-y@2) = x
rget=$(call get,$(call opp,$(1)))
###
default=$(if $($(1)_$(2)),$($(1)_$(2)),$($(1)))
solve=$(HOST_$(1))
solve_ip=$(IP_$(1))
# can be redefined in conf.mk if that's not the expected behaviour
display?=host $(1) aka $(call solve,$(1))

# log file name
log=$(addprefix log/,$(notdir $(1)))

#################### set variables after conf.mk
ifeq "$(SSH_KEY)" ""
SSH_KEY_OPTION ?=
else
SSH_KEY_OPTION ?= -i $(SSH_KEY)
endif

SSH_OPTIONS ?= $(SSH_KEY_OPTION) -l $(SLICE)
SSH = ssh $(SSH_OPTIONS)

SUDO ?= sudo -S

ALL_NODE_IDS=$(sort $(foreach link,$(LINKS),$(call leftnode,$(link))) $(foreach link,$(LINKS),$(call rightnode,$(link))))
ALL_NODE_HOSTS=$(foreach id,$(ALL_NODE_IDS),$(call solve,$(id)))
ALL_LINK_IDS=$(addsuffix @1,$(LINKS)) $(addsuffix @2,$(LINKS))
CONF_NODE_IDS=$(subst HOST_,,$(filter HOST_%,$(.VARIABLES)))

####################
init_all: init all
init:
	@[ -d L ] || ( echo Creating tmp dir L; mkdir L)
	@[ -d log ] || (echo Creating tmp dir log; mkdir log)
	@[ -d cache ] || ( echo Creating tmp dir cache; mkdir cache)
.PHONY: init_all init

FORCE:

.SECONDARY:

LINKTARGETS=$(addprefix L/,$(LINKS))
all: $(LINKTARGETS)
.PHONY: all

# could also do make ++SLICE
showslice: ++SLICE FORCE

shownodes:
	@$(foreach id,$(ALL_NODE_IDS),echo $(id)=$(call display,$(id));)
showhostnames: ++ALL_NODE_HOSTS
showips:
	@$(foreach id,$(ALL_NODE_IDS),echo $(id)=$(call display,$(id)) has ip/network set to $(IP_$(id));)
showlinks:
	@$(foreach link,$(LINKS), echo $(call display,$(call leftnode,$(link))) '====>' $(call display,$(call rightnode,$(link)));)
showcontrollers:
	@$(foreach id,$(ALL_NODE_IDS), echo $(call display,$(id)) has controller $(call default,CONTROLLER,$(id));)
.PHONY: shownodes showhostnames showips showlinks showcontrollers

sshchecks: $(foreach id,$(ALL_NODE_IDS),sshcheck-$(id))
.PHONY: sshchecks
ovsversions: $(foreach id,$(ALL_NODE_IDS),ovsversion-$(id))
.PHONY: ovsversions
# more general form; run with  make sshs SSH-COMMAND="bla bla bla"
sshs: $(foreach id,$(ALL_NODE_IDS),ssh-$(id))
.PHONY: sshs

DBS=$(foreach id,$(ALL_NODE_IDS),cache/db.$(id))
dbs: $(DBS)
.PHONY: dbs

SWITCHS=$(foreach id,$(ALL_NODE_IDS),cache/switch.$(id))
switchs: $(SWITCHS)
.PHONY: switchs

start: dbs switchs
.PHONY: start

stop:$(foreach id,$(ALL_NODE_IDS),cache/stop.$(id))
.PHONY: stop

status:$(foreach id,$(ALL_NODE_IDS),cache/status.$(id))
.PHONY: status

BRIDGES=$(foreach id,$(ALL_NODE_IDS),cache/bridge.$(id))
bridges: $(BRIDGES)
.PHONY: bridges

CONTROLLERS=$(foreach id,$(ALL_NODE_IDS),cache/controller.$(id))
controllers: $(CONTROLLERS)
.PHONY: controllers

del-controllers: $(foreach id,$(ALL_NODE_IDS),del-controller-$(id))
.PHONY: del-controllers

### node-oriented targets
# check ssh connectivity
sshcheck-%: FORCE
	@if $(SSH) $(HOST_$*) hostname &> /dev/null; then echo "ssh on" $(call display,$*) "OK" ; \
	 else echo "ssh on" $(call display,$*) "KO !!!"; fi

ovsversion-%: FORCE
	@OVSVERSION=$$($(SSH) $(HOST_$*) rpm -q sliver-openvswitch); echo $(call display,$*) has $$OVSVERSION

ssh-%: FORCE
	@$(SSH) $(HOST_$*) $(SUDO) $(SSH-COMMAND)

# should probably replace sshcheck
cache/status.%: FORCE
	@echo "=== DB and SWITCH processes on $(call display,$*)"
	@$(SSH) $(HOST_$*) $(SUDO) sliver-ovs status ||:

cache/host.%:
	@echo "IP lookup for $(call display,$*)"
	@host $(HOST_$*) | sed -n 's/^.*has address *//p' > $@

cache/db.%:
	@echo "Starting db server on $(call display,$*) - logs in $(call log,$@)"
	@$(SSH) $(HOST_$*) $(SUDO) sliver-ovs start-db &> $(call log,$@) && touch $@

cache/switch.%: | cache/db.%
	@echo "Starting vswitchd on $(call display,$*) - logs in $(call log,$@)"
	@$(SSH) $(HOST_$*) $(SUDO) sliver-ovs start-switch &> $(call log,$@) && touch $@

cache/bridge.%: | cache/db.%
	@echo "Creating bridge on $(call display,$*) - logs in $(call log,$@)"
	@$(SSH) $(HOST_$*) $(SUDO) \
		sliver-ovs create-bridge $(BRIDGE) $(IP_$*) $(call default,BROPTIONS,$*) &> $(call log,$@) \
	 && touch $@

cache/controller.%: cache/bridge.%
	@echo "Setting controller $(call default,CONTROLLER,$*) on $(call display,$*) - logs in $(call log,$@)"
	@$(SSH) $(HOST_$*) $(SUDO) ovs-vsctl set-controller $(BRIDGE) $(call default,CONTROLLER,$*) &> $(call log,$@) \
	 && touch $@

# xxx this probably needs a more thorough cleanup in cache/
cache/stop.%: del-bridge.%
	@echo "Stopping switch & db on $(call display,$*)"
	@$(SSH) $(HOST_$*) $(SUDO) sliver-ovs stop && rm cache/switch.$* cache/db.$*

### link-oriented targets
# L/<nodeid>-<node_id>
L/%: cache/endpoint.%@1 cache/endpoint.%@2
	@touch $@
	@echo "Link $* is up"

U/%: del-iface.%@1 del-iface.%@2
	@rm -f L/$*
	@echo "Deleted link $*"

del-bridge.%: | cache/db.%
	@echo "Deleting bridge on $(call display,$*)"
	@$(SSH) $(HOST_$*) $(SUDO) sliver-ovs del-bridge $(BRIDGE);
	@rm -f cache/bridge.$* \
	      cache/iface.$*$(SEP)*@1 cache/iface.*$(SEP)$*@2 \
	      cache/port.$*$(SEP)*@1  cache/port.*$(SEP)$*@2  \
	      cache/endpoint.$*$(SEP)*@?  cache/endpoint.*$(SEP)$*@?  \
	      L/$*$(SEP)*	      L/*$(SEP)$*

del-switch.%:
	@echo "Shutting down switch on $(call display,$*)"
	@$(SSH) $(HOST_$*) $(SUDO) sliver-ovs stop-switch
	@rm -f cache/switch.$* \
	       cache/port.$*$(SEP)*@1  cache/port.*$(SEP)$*@2  \
	       cache/endpoint.$*$(SEP)*@2  cache/endpoint.*$(SEP)$*@1  \
	       L/$*$(SEP)*	      L/*$(SEP)$*

del-db.%:
	@echo "Shutting down db on $(call display,$*)"
	@$(SSH) $(HOST_$*) $(SUDO) sliver-ovs stop-db
	@rm -f cache/db.$*

del-controller-%: | cache/db.%
	@echo "Removing controller for $(call display,$*)"
	@$(SSH) $(HOST_$*) $(SUDO) sliver-ovs del-controller $(BRIDGE)
	@rm -f cache/controller.$*

del-links: $(addprefix U/,$(notdir $(wildcard L/*)))

del-switchs: $(addprefix del-,$(notdir $(wildcard cache/switch.*)))

del-dbs: $(addprefix del-,$(notdir $(wildcard cache/db.*)))

shutdown: del-switchs del-dbs

.PHONY: del-links del-switchs del-dbs shutdown


### snapshots
remote-snapshot: remote-snapshot-ips remote-snapshot-links
	@true

remote-snapshot-ips: $(addprefix cache/rsnap.ip.,$(CONF_NODE_IDS))
	@cat /dev/null $^

remote-snapshot-links: $(addprefix cache/rsnap.links.,$(CONF_NODE_IDS))
	@sort -u /dev/null $^

### update sliver-ovs
update: $(addprefix update-,$(CONF_NODE_IDS))
	@true

update-%: FORCE
	@[ -n "$(SLIVER_OVS)" ] || { echo "SLIVER_OVS not set" >&2; exit 1; }
	@[ -f "$(SLIVER_OVS)" ] || { echo "$(SLIVER_OVS) not found" >&2; exit 1; }
	@echo "Sending $(SLIVER_OVS) to $(call display,$*)"
	@scp -q $(SLIVER_OVS) $(SLICE)@$(call solve,$*):
	@$(SSH) $(call solve,$*) $(SUDO) mv $(notdir $(SLIVER_OVS)) /usr/sbin/sliver-ovs

.PHONY: remote-snapshot remote-snapshot-ips remote-snapshot-links

cache/rsnap.ip.%: 
	@$(SSH) $(HOST_$*) $(SUDO) ovs-vsctl -- \
		get interface $(BRIDGE) options:local_ip options:local_netmask |\
		sed -n 's|"||g;1h;2{G;s|\(.*\)\n\(.*\)|IP_$* = \2/\1|p}' > $@ || \
	 { rm $@; exit 1; }

cache/rsnap.links.%: 
	@$(SSH) $(HOST_$*) $(SUDO) ovs-vsctl list-ifaces $(BRIDGE) | \
	 sed -n 's/^L/LINKS += /p' > $@ || \
	 { rm $@; exit 1; }

.SECONDEXPANSION:

del-iface.%: 
del-iface.%: | cache/db.$$(call get,%)
	@echo "Removing interface for link $(call linkpart,$*) from $(call get,$*)"
	@$(SSH) $(HOST_$(call get,$*)) \
		$(SUDO) sliver-ovs del-port L$(call linkpart,$*)
	@rm -f cache/iface.$* cache/port.$* cache/endpoint.$* cache/endpoint.$(call opp,$*)


### '%' here is leftid-rightid@{1,2}
# we retrieve % as $(*F)
#linkid=$(call linkpart,%)
#nodeid=$(call get,%)
#bridgefile=cache/bridge.$(nodeid)
cache/iface.%: cache/bridge.$$(call get,%) | cache/db.$$(call get,%)
	@echo "Creating interface for link $(call linkpart,$(*F)) on $(call display,$(call get,$(*F))) - logs in $(call log,$@)"
	@$(SSH) $(call solve,$(call get,$(*F))) $(SUDO) sliver-ovs create-port $(BRIDGE) \
		L$(call linkpart,$(*F)) &> $(call log,$@) \
	 && touch $@

cache/port.%: cache/iface.% cache/switch.$$(call get,%)
	@echo "Getting port number for link $(call linkpart,$(*F)) on $(call display,$(call get,$(*F))) - logs in $(call log,$@)"
	@$(SSH) $(call solve,$(call get,$(*F))) $(SUDO) \
		ovs-appctl netdev-tunnel/get-port L$(call linkpart,$(*F)) > $@ 2> $(call log,$@) \
	 || { rm $@; exit 1; }


# linkid=$(call linkpart,%)
# nodeid=$(call get,%)
# iface1=cache/iface.%
# iface2=cache/iface.$(call opp,%)
cache/endpoint.%: cache/host.$$(call rget,%) cache/port.$$(call opp,%) cache/iface.%
	@echo "Setting port number of link $(call linkpart,$(*F)) on $(call display,$(call get,$(*F))) - logs in $(call log,$@)"
	@$(SSH) $(call solve,$(call get,$(*F))) $(SUDO) sliver-ovs set-remote-endpoint L$(call linkpart,$(*F)) \
			$$(cat cache/host.$(call rget,$(*F))) \
			$$(cat cache/port.$(call opp,$(*F))) 2> $(call log,$@) \
	 && touch $@

####################
CLEANTARGETS=$(addprefix del-,$(notdir $(wildcard cache/bridge.*)))
clean: $(CLEANTARGETS)
distclean:
	rm -rf L log cache
.PHONY: clean distclean

####################
graph.dot:
	( echo "digraph $(SLICE) {"; ls L | sed 's/$(SEP)/->/;s/$$/;/'; echo "}" ) > $@
graph.ps: graph.dot
	dot -Tps < $^ > $@	

####################
# googlemap stuff
# Uses:
# GMAP_SERVER, the hostname that runs an ndnmap instance
# GMAP_PROBES, a list of tuples of the form
# <linkid>=<nodeid>=<internal_linkid>=<frequency>
# e.g.
# SENDER-MUX=SENDER=3=0.5
# which would mean, the link 'SENDER-MUX' should be monitored at the 'SENDER' node and is coded 
# in links.json with id=3, every half second
# sliver-ovs gmap-probe is designed to run in background,
# and so that a new run will kill any previously running instance
gprobes: $(foreach probe,$(GMAP_PROBES),gprobe-$(probe))

gprobe-%: FORCE
	$(SSH) -n $(call solve,$(word 2,$(subst @, ,$(*F)))) $(SUDO) \
	sliver-ovs gmap-probe L$(word 1,$(subst @, ,$(*F))) $(GMAP_SERVER) $(word 3,$(subst @, ,$(*F))) $(word 4,$(subst @, ,$(*F)))

####################
# 'virtual' targets in that there's no real file attached
define node_shortcuts
sshcheck.$(1): sshcheck-$(1) FORCE
db.$(1): cache/db.$(1) FORCE
switch.$(1): cache/switch.$(1) FORCE
start.$(1): cache/start.$(1) FORCE
stop.$(1): cache/stop.$(1) FORCE
status.$(1): cache/status.$(1) FORCE
bridge.$(1): cache/bridge.$(1) FORCE
host.$(1): cache/host.$(1) FORCE
# switch already depends on db, but well
cache/start.$(1): cache/db.$(1) cache/switch.$(1) FORCE
endef

$(foreach id,$(ALL_NODE_IDS), $(eval $(call node_shortcuts,$(id))))

define link_shortcuts
iface.%: cache/iface.%
endpoint.%: cache/endpoint.%
endef

$(foreach id,$(ALL_LINK_IDS), $(eval $(call link_shortcuts,$(id))))

help:
	@cat Makefile.help

#################### convenience, for debugging only
# make +foo : prints the value of $(foo)
# make ++foo : idem but verbose, i.e. foo=$(foo)
++%: varname=$(subst +,,$@)
++%:
	@echo "$(varname)=$($(varname))"
+%: varname=$(subst +,,$@)
+%:
	@echo "$($(varname))"
