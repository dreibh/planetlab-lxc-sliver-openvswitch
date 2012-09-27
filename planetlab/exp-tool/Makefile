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
ALL_LINK_IDS=$(addsuffix @1,$(LINKS)) $(addsuffix @2,$(LINKS))
CONF_NODE_IDS=$(subst HOST_,,$(filter HOST_%,$(.VARIABLES)))

####################
all+init: init all
init:
	@[ -d L ] || ( echo Creating tmp dir L; mkdir L)
	@[ -d log ] || (echo Creating tmp dir log; mkdir log)
	@[ -d cache ] || ( echo Creating tmp dir cache; mkdir cache)
.PHONY: all+init init

FORCE:

.SECONDARY:

LINKTARGETS=$(addprefix L/,$(LINKS))
all: $(LINKTARGETS)
.PHONY: all

# could also do make ++SLICE
showslice: ++SLICE FORCE

shownodes:
	@$(foreach id,$(ALL_NODE_IDS),echo $(id)=$(call display,$(id));)
showips:
	@$(foreach id,$(ALL_NODE_IDS),echo $(id)=$(call display,$(id)) has ip/network set to $(IP_$(id));)
showlinks:
	@$(foreach link,$(LINKS), echo $(call display,$(call leftnode,$(link))) '====>' $(call display,$(call rightnode,$(link)));)
.PHONY: shownodes showips showlinks

sshchecks: $(foreach id,$(ALL_NODE_IDS),cache/sshcheck.$(id))
.PHONY: sshchecks

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

### node-oriented targets
# check ssh connectivity
cache/sshcheck.%: FORCE
	@if $(SSH) $(HOST_$*) hostname 2> /dev/null; then echo "ssh on" $(call display,$*) "OK" ; \
	 else echo "ssh on" $(call display,$*) "KO !!!"; fi

# should probably replace sshcheck
cache/status.%: FORCE
	@echo "=== DB and SWITCH processes on $(call display,$*)"
	@$(SSH) $(HOST_$*) $(SUDO) sliver-ovs status

cache/host.%:
	@echo "IP lookup for $(call display,$*)"
	@host $(HOST_$*) | sed -n 's/^.*has address *//p' > $@

cache/db.%:
	@echo "Starting db server on $(call display,$*) - logs in $(call log,$@)"
	@$(SSH) $(HOST_$*) $(SUDO) sliver-ovs start-db &> $(call log,$@) && touch $@

cache/switch.%: cache/db.%
	@echo "Starting vswitchd on $(call display,$*) - logs in $(call log,$@)"
	@$(SSH) $(HOST_$*) $(SUDO) sliver-ovs start-switch &> $(call log,$@) && touch $@

cache/bridge.%: cache/switch.%
	@echo "Creating bridge on $(call display,$*) - logs in $(call log,$@)"
	@$(SSH) $(HOST_$*) $(SUDO) sliver-ovs create-bridge $(BRIDGE) $(IP_$*) > $@ 2> $(call log,$@) || { rm $@; exit 1; }
	@echo Created bridge with tap $$(cat $@) on $(call display,$*)

cache/controller.%: cache/bridge.%
	@echo "Setting controller on $(call display,$*) - logs in $(call log,$@)"
	@$(SSH) $(HOST_$*) $(SUDO) sliver-ovs set-controller $(BRIDGE) $(CONTROLLER_$*) &> $(call log,$@) || { rm $@; exit 1; }

# xxx this probably needs a more thorough cleanup in cache/
cache/stop.%: del-bridge.%
	@echo "Stopping switch & db on $(call display,$*)"
	@$(SSH) $(HOST_$*) $(SUDO) sliver-ovs stop && rm cache/switch.% cache/db.%

### link-oriented targets
# L/<nodeid>-<node_id>
L/%: cache/link.%@1 cache/link.%@2
	@touch $@
	@echo "Created link $*"

U/%: del-iface.%@1 del-iface.%@2
	@rm -f L/$*
	@echo "Deleted link $*"

del-bridge.%: cache/db.%
	@echo "Deleting bridge on $(call display,$*)"
	@$(SSH) $(HOST_$*) $(SUDO) sliver-ovs del-bridge $(BRIDGE);
	@rm -f cache/bridge.$* \
	      cache/iface.$*$(SEP)*@1 cache/iface.*$(SEP)$*@2 \
	      cache/link.$*$(SEP)*@?  cache/link.*$(SEP)$*@?  \
	      L/$*$(SEP)*	      L/*$(SEP)$*

del-switch.%: del-bridge.%
	@echo "Shutting down switch on $(call display,$*)"
	@$(SSH) $(HOST_$*) $(SUDO) sliver-ovs stop-switch
	@rm -f cache/switch.$* 

del-db.%:
	@echo "Shutting down db on $(call display,$*)"
	@$(SSH) $(HOST_$*) $(SUDO) sliver-ovs stop-db
	@rm -f cache/db.$*

del-links: $(addprefix U/,$(notdir $(filter-out %.log,$(wildcard L/*))))

del-switchs: $(addprefix del-,$(notdir $(filter-out %.log,$(wildcard cache/switch.*))))

del-dbs: $(addprefix del-,$(notdir $(filter-out %.log,$(wildcard cache/db.*))))

shutdown: del-switchs del-dbs

.PHONY: del-links del-switchs del-dbs shutdown


### snapshots
remote-snapshot: remote-snapshot-ips remote-snapshot-links
	@true

remote-snapshot-ips: $(addprefix cache/rsnap.ip.,$(CONF_NODE_IDS))
	@cat /dev/null $^

remote-snapshot-links: $(addprefix cache/rsnap.links.,$(CONF_NODE_IDS))
	@sort -u /dev/null $^

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

del-iface.%: cache/db.$$(call get,%)
	@echo "Removing interface for link $(call linkpart,$*) from $(call get,$*)"
	@$(SSH) $(HOST_$(call get,$*)) \
		$(SUDO) sliver-ovs del-port L$(call linkpart,$*)
	@rm -f cache/iface.$* cache/link.$* cache/link.$(call opp,$*)


### '%' here is leftid-rightid@{1,2}
# we retrieve % as $(*F)
#linkid=$(call linkpart,%)
#nodeid=$(call get,%)
#bridgefile=cache/bridge.$(nodeid)
cache/iface.%: cache/bridge.$$(call get,%)
	@echo "Creating interface for link $(call linkpart,$(*F)) on $(call display,$(call get,$(*F))) - logs in $(call log,$@)"
	@$(SSH) $(call solve,$(call get,$(*F))) $(SUDO) sliver-ovs create-port $(BRIDGE) \
		L$(call linkpart,$(*F)) > $@ 2> $(call log,$@) || { rm $@; exit 1; }


# linkid=$(call linkpart,%)
# nodeid=$(call get,%)
# iface1=cache/iface.%
# iface2=cache/iface.$(call opp,%)
cache/link.%: cache/host.$$(call rget,%) cache/iface.% cache/iface.$$(call opp,%)
	@echo "Setting port number of link $(call linkpart,$(*F)) on $(call display,$(call get,$(*F))) - logs in $(call log,$@)"
	@$(SSH) $(call solve,$(call get,$(*F))) $(SUDO) sliver-ovs set-remote-endpoint L$(call linkpart,$(*F)) \
			$$(cat cache/host.$(call rget,$(*F))) \
			$$(cat cache/iface.$(call opp,$(*F))) 2> $(call log,$@) \
	 && touch $@

####################
CLEANTARGETS=$(addprefix del-,$(notdir $(filter-out %.log,$(wildcard cache/bridge.*))))
clean: $(CLEANTARGETS)
distclean:
	rm -rf L U cache
.PHONY: clean distclean

####################
graph.dot:
	( echo "digraph $(SLICE) {"; ls L | sed 's/$(SEP)/->/;s/$$/;/'; echo "}" ) > $@
graph.ps: graph.dot
	dot -Tps < $^ > $@	

####################
# 'virtual' targets in that there's no real file attached
define node_shortcuts
sshcheck.$(1): cache/sshcheck.$(1) FORCE
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
link.%: cache/link.%
endef

$(foreach id,$(ALL_LINK_IDS), $(eval $(call link_shortcuts,$(id))))

#################### convenience, for debugging only
# make +foo : prints the value of $(foo)
# make ++foo : idem but verbose, i.e. foo=$(foo)
++%: varname=$(subst +,,$@)
++%:
	@echo "$(varname)=$($(varname))"
+%: varname=$(subst +,,$@)
+%:
	@echo "$($(varname))"
