include conf.mk

# proj1(x@y) = x
proj1=$(word 1,$(subst @, ,$(1)))

# proj2(x@y) = y
proj2=$(word 2,$(subst @, ,$(1)))

# get(x-y@1) = x
# get(x-y@2) = y
get=$(word $(call proj2,$(1)),$(subst -, ,$(call proj1,$(1))))

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

SSH_OPTIONS ?= $(SSH_KEY_OPTION) -l $(SLICE) -t
SSH = ssh $(SSH_OPTIONS)

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
.PHONY: shownodes

sshchecks: $(foreach id,$(ALL_NODE_IDS),cache/sshcheck.$(id))
.PHONY: sshchecks

dbs: $(foreach id,$(ALL_NODE_IDS),cache/db.$(id))
.PHONY: dbs

switchds: $(foreach id,$(ALL_NODE_IDS),cache/switchd.$(id))
.PHONY: switchds

### node-oriented targets
cache/host.%:
	@echo "IP lookup for $(call display,$*)"
	@host $(HOST_$*) | sed -n 's/^.*has address *//p' > $@

cache/db.%:
	@echo "Starting db server on $(call display,$*)"
	@$(SSH) $(HOST_$*) sudo sliver-ovs start-db > $@ \
	 || { rm $@; exit 1; }

cache/switchd.%: cache/db.%
	@echo "Starting vswitchd on $(call display,$*)"
	@$(SSH) $(HOST_$*) sudo sliver-ovs start-switch > $@ \
	 || { rm $@; exit 1; }

cache/bridge.%: cache/switchd.%
	@echo "Creating bridge on $(call display,$*)"
	@$(SSH) $(HOST_$*) sudo sliver-ovs create-bridge $(IP_$*) > $@ \
	 || { rm $@; exit 1; }

cache/sshcheck.%: FORCE
	@if $(SSH) $(HOST_$*) hostname 2> /dev/null; then echo "ssh on" $(call display,$*) "OK" ; \
	 else echo "ssh on" $(call display,$*) "KO !!!"; fi

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
		$(SSH) $(HOST_$*) sudo sliver-ovs del-bridge $$(cat cache/bridge.$*);\
	 fi
	@rm -f cache/bridge.$* \
	      cache/iface.$*-*@1 cache/iface.*-$*@2 \
	      cache/link.$*-*@?  cache/link.*-$*@?  \
	      L/$*-*		 L/*-$*

del-switchd.%: del-bridge.%
	@echo "Shutting down switchd on $(call display,$*)"
	@if [ -f cache/switchd.$* ]; then \
		$(SSH) $(HOST_$*) sudo ovs-appctl exit;\
	 fi
	@rm -f cache/switchd.$*

del-db.%:
	@echo "Shutting down db on $(call display,$*)"
	@if [ -f cache/db.$* ]; then \
		$(SSH) $(HOST_$*) sudo ovs-appctl --target=$(VARRUN)/ovsdb-server.$$(cat cache/db.$*).ctl exit;\
	 fi
	@rm -f cache/db.$*

del-links: $(addprefix U/,$(notdir $(wildcard L/*)))

del-switchds: $(addprefix del-,$(notdir $(wildcard cache/switchd.*)))

del-dbs: $(addprefix del-,$(notdir $(wildcard cache/db.*)))

shutdown: del-switches del-dbs

.PHONY: del-links del-switchds del-dbs shutdown

####################
graph.dot:
	( echo "digraph $(SLICE) {"; ls L | sed 's/-/->/;s/$$/;/'; echo "}" ) > $@
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
		sudo sliver-ovs del-port L$(call proj1,$*)
	@rm -f cache/iface.$* cache/link.$* cache/link.$(call opp,$*)



cache/iface.%: cache/bridge.$$(call get,%)
	@echo "Creating interface for link $(call proj1,$*) on $(call get,$*)"
	@$(SSH) $(HOST_$(call get,$*)) \
		sudo sliver-ovs create-port $$(cat $^) L$(call proj1,$*) > $@ \
	 || { rm $@; exit 1; }

cache/link.%: cache/host.$$(call rget,$$*) cache/iface.% cache/iface.$$(call opp,$$*)
	@echo "Setting port number of link $(call proj1,$*) on $(call get,$*)"
	@$(SSH) $(HOST_$(call get,$*)) \
		sudo ovs-vsctl set interface L$(call proj1,$*) \
			options:remote_ip=$$(cat cache/host.$(call rget,$*)) \
			options:remote_port=$$(cat cache/iface.$(call opp,$*)) \
	 && touch $@

#################### convenience, for debugging only
# make +foo : prints the value of $(foo)
# make ++foo : idem but verbose, i.e. foo=$(foo)
++%: varname=$(subst +,,$@)
++%:
	@echo "$(varname)=$($(varname))"
+%: varname=$(subst +,,$@)
+%:
	@echo "$($(varname))"
