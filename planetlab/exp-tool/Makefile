include conf.mk


# proj1(x@y) = x
proj1=$(word 1,$(subst @, ,$(1)))

# proj2(x@y) = y
proj2=$(word 2,$(subst @, ,$(1)))

SEP?=-
# get(x-y@1) = x
# get(x-y@2) = y
get=$(word $(call proj2,$(1)),$(subst $(SEP), ,$(call proj1,$(1))))

# flip(1) = 2
# flip(2) = 1
flip=$(if $(findstring 1,$(1)),2,1)

# opp(x-y@1) = x-y@2
# opp(x-y@2) = x-y@1
opp=$(call proj1,$(1))@$(call flip,$(call proj2,$(1)))

# rget(x-y@1) = y
# rget(x-y@2) = x
rget=$(call get,$(call opp,$(1)))

solve=$(HOST_$(1))
solve_ip=$(IP_$(1))
# can be redefined in conf.mk if that's not the expected behaviour
display?=host $(1) aka $(call solve,$(1))

#################### set variables after conf.mk
ifeq "$(SSH_KEY)" ""
SSH_KEY_OPTION ?=
else
SSH_KEY_OPTION ?= -i $(SSH_KEY)
endif

#SSH_OPTIONS ?= $(SSH_KEY_OPTION) -l $(SLICE) -t
SSH_OPTIONS ?= $(SSH_KEY_OPTION) -l $(SLICE)
SSH = ssh $(SSH_OPTIONS)

SUDO ?= sudo -S

ALL_NODE_IDS=$(shell (cut -d- -f1 < links; cut -d- -f2 < links) | sort -u)

####################
all+init: init all
init:
	@[ -d L ] || ( echo Creating tmp dir L; mkdir L)
	@[ -d U ] || ( echo Creating tmp dir U; mkdir U)
	@[ -d cache ] || ( echo Creating tmp dir cache; mkdir cache)
.PHONY: all+init init

FORCE:

.SECONDARY:

all: $(addprefix L/,$(shell cat links))
.PHONY: all

# could also do make ++SLICE
showslice: ++SLICE FORCE

shownodes:
	@$(foreach id,$(ALL_NODE_IDS),echo $(id)=$(call display,$(id));)
showips:
	@$(foreach id,$(ALL_NODE_IDS),echo $(id)=$(call display,$(id)) has ip/network set to $(IP_$(id));)
.PHONY: shownodes showips

sshchecks: $(foreach id,$(ALL_NODE_IDS),cache/sshcheck.$(id))
.PHONY: sshchecks

dbs: $(foreach id,$(ALL_NODE_IDS),cache/db.$(id))
.PHONY: dbs

switchs: $(foreach id,$(ALL_NODE_IDS),cache/switch.$(id))
.PHONY: switchs

start: dbs switchs
.PHONY: start

stop:$(foreach id,$(ALL_NODE_IDS),cache/stop.$(id))
.PHONY: stop

status:$(foreach id,$(ALL_NODE_IDS),cache/status.$(id))
.PHONY: status

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
	@echo "Starting db server on $(call display,$*) - logs in $@.log"
	@$(SSH) $(HOST_$*) $(SUDO) sliver-ovs start-db &> $@.log && touch $@

cache/switch.%: cache/db.%
	@echo "Starting vswitchd on $(call display,$*) - logs in $@.log"
	@$(SSH) $(HOST_$*) $(SUDO) sliver-ovs start-switch &> $@.log && touch $@

cache/bridge.%: cache/switch.%
	@echo "Creating bridge on $(call display,$*) - logs in $@.log"
	@$(SSH) $(HOST_$*) $(SUDO) sliver-ovs create-bridge $(IP_$*) &> $@.log && touch $@

# xxx this probably needs a more thorough cleanup in cache/
cache/stop.%: del-bridge.%
	@echo "Stopping switch & db on $(call display,$*)"
	@$(SSH) $(HOST_$*) $(SUDO) sliver-ovs stop && rm cache/switch.% cache/db.%

### link-oriented targets
L/%: cache/link.%@1 cache/link.%@2
	@touch $@
	@echo "Created link $*"

U/%: del-iface.%@1 del-iface.%@2
	@rm -f L/$*
	@echo "Deleted link $*"

del-bridge.%: cache/db.%
	@echo "Deleting bridge on $(call display,$*)"
	@if [ -f cache/bridge.$* ]; then \
		$(SSH) $(HOST_$*) $(SUDO) sliver-ovs del-bridge $$(cat cache/bridge.$*);\
	 fi
	@rm -f cache/bridge.$* \
	      cache/iface.$*$(SEP)*@1 cache/iface.*$(SEP)$*@2 \
	      cache/link.$*$(SEP)*@?  cache/link.*$(SEP)$*@?  \
	      L/$*$(SEP)*	      L/*$(SEP)$*

del-switch.%: del-bridge.%
	@echo "Shutting down switch on $(call display,$*)"
	@if [ -f cache/switch.$* ]; then \
		$(SSH) $(HOST_$*) $(SUDO) ovs-appctl exit;\
	 fi
	@rm -f cache/switch.$*

del-db.%:
	@echo "Shutting down db on $(call display,$*)"
	@if [ -f cache/db.$* ]; then \
		$(SSH) $(HOST_$*) $(SUDO) ovs-appctl --target=$(VARRUN)/ovsdb-server.$$(cat cache/db.$*).ctl exit;\
	 fi
	@rm -f cache/db.$*

del-links: $(addprefix U/,$(notdir $(wildcard L/*)))

del-switchs: $(addprefix del-,$(notdir $(wildcard cache/switch.*)))

del-dbs: $(addprefix del-,$(notdir $(wildcard cache/db.*)))

shutdown: del-switches del-dbs

.PHONY: del-links del-switchs del-dbs shutdown

####################
graph.dot:
	( echo "digraph $(SLICE) {"; ls L | sed 's/$(SEP)/->/;s/$$/;/'; echo "}" ) > $@
graph.ps: graph.dot
	dot -Tps < $^ > $@	

####################
clean: $(addprefix del-,$(notdir $(wildcard cache/bridge.*)))
distclean:
	rm -rf L U cache

.PHONY: clean distclean

.SECONDEXPANSION:

del-iface.%: cache/db.$$(call get,%)
	@echo "Removing interface for link $(call proj1,$*) from $(call get,$*)"
	@$(SSH) $(HOST_$(call get,$*)) \
		$(SUDO) sliver-ovs del-port L$(call proj1,$*)
	@rm -f cache/iface.$* cache/link.$* cache/link.$(call opp,$*)



cache/iface.%: cache/bridge.$$(call get,%)
	@echo "Creating interface for link $(call proj1,$*) on $(call get,$*)"
	@$(SSH) $(HOST_$(call get,$*)) \
		$(SUDO) sliver-ovs create-port $$(cat $^) L$(call proj1,$*) > $@ \
	 || { rm $@; exit 1; }

cache/link.%: cache/host.$$(call rget,$$*) cache/iface.% cache/iface.$$(call opp,$$*)
	@echo "Setting port number of link $(call proj1,$*) on $(call get,$*)"
	@$(SSH) $(HOST_$(call get,$*)) \
		$(SUDO) ovs-vsctl set interface L$(call proj1,$*) \
			options:remote_ip=$$(cat cache/host.$(call rget,$*)) \
			options:remote_port=$$(cat cache/iface.$(call opp,$*)) \
	 && touch $@

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
# switch already depends on db, but well
cache/start.$(1): cache/db.$(1) cache/switch.$(1) FORCE
endef

$(foreach id,$(ALL_NODE_IDS), $(eval $(call node_shortcuts,$(id))))

#define link_shortcuts
#
#endef

#################### convenience, for debugging only
# make +foo : prints the value of $(foo)
# make ++foo : idem but verbose, i.e. foo=$(foo)
++%: varname=$(subst +,,$@)
++%:
	@echo "$(varname)=$($(varname))"
+%: varname=$(subst +,,$@)
+%:
	@echo "$($(varname))"
