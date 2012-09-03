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


.SECONDARY:

all: $(addprefix L/,$(shell cat links))


cache/host.%:
	@echo "IP lookup for host $*"
	@host $(HOST_$*) | sed -n 's/^.*has address *//p' > $@

cache/db.%:
	@echo "Starting db server on host $*"
	@ssh -t -l $(SLICE) $(HOST_$*) \
		sudo start_ovsdb-server > $@ \
	 || { rm $@; exit 1; }

cache/switchd.%: cache/db.%
	@echo "Starting vswitchd on host $*"
	@ssh -t -l $(SLICE) $(HOST_$*) \
		sudo start_vswitchd > $@ \
	 || { rm $@; exit 1; }

cache/bridge.%: cache/switchd.%
	@echo "Creating bridge on host $*"
	@ssh -t -l $(SLICE) $(HOST_$*) \
		sudo create_bridge $(IP_$*) > $@ \
	 || { rm $@; exit 1; }


L/%: cache/link.%@1 cache/link.%@2
	@touch $@
	@echo "Created link $*"

U/%: del-iface.%@1 del-iface.%@2
	@rm -f L/$*
	@echo "Deleted link $*"

del-bridge.%: cache/db.%
	@echo "Deleting bridge on host $*"
	@if [ -f cache/bridge.$* ]; then \
		ssh -t -l $(SLICE) $(HOST_$*) \
			sudo del_bridge $$(cat cache/bridge.$*);\
	 fi
	@rm -f cache/bridge.$* \
	      cache/iface.$*-*@1 cache/iface.*-$*@2 \
	      cache/link.$*-*@?  cache/link.*-$*@?  \
	      L/$*-*		 L/*-$*

del-switchd.%: del-bridge.%
	@echo "Shutting down switchd on host $*"
	@if [ -f cache/switchd.$* ]; then \
		ssh -t -l $(SLICE) $(HOST_$*) \
			sudo ovs-appctl exit;\
	 fi
	@rm -f cache/switchd.$*

del-db.%:
	@echo "Shutting down db on host $*"
	@if [ -f cache/db.$* ]; then \
		ssh -t -l $(SLICE) $(HOST_$*) \
			sudo ovs-appctl --target=$(VARRUN)/ovsdb-server.$$(cat cache/db.$*).ctl exit;\
	 fi
	@rm -f cache/db.$*

shutdown: del-switches del-dbs

graph.dot:
	( echo "digraph $(SLICE) {"; ls L | sed 's/-/->/;s/$$/;/'; echo "}" ) > $@

graph.ps: graph.dot
	dot -Tps < $^ > $@	

.PHONY: clean del-links graph.dot servers


clean: $(addprefix del-,$(notdir $(wildcard cache/bridge.*)))

del-links: $(addprefix U/,$(notdir $(wildcard L/*)))

switchds: $(wildcard cache/switchd.*)

dbs: $(wildcard cache/db.*)

del-switchds: $(addprefix del-,$(notdir $(wildcard cache/switchd.*)))

del-dbs: $(addprefix del-,$(notdir $(wildcard cache/db.*)))

.SECONDEXPANSION:

del-iface.%: cache/db.$$(call get,%)
	@echo "Removing interface for link $(call proj1,$*) from host $(call get,$*)"
	@ssh -t -l $(SLICE) $(HOST_$(call get,$*)) \
		sudo del_port L$(call proj1,$*)
	@rm -f cache/iface.$* cache/link.$* cache/link.$(call opp,$*)



cache/iface.%: cache/bridge.$$(call get,%)
	@echo "Creating interface for link $(call proj1,$*) on host $(call get,$*)"
	@ssh -t -l $(SLICE) $(HOST_$(call get,$*)) \
		sudo create_port $$(cat $^) L$(call proj1,$*) > $@ \
	 || { rm $@; exit 1; }

cache/link.%: cache/host.$$(call rget,$$*) cache/iface.% cache/iface.$$(call opp,$$*)
	@echo "Setting port number of link $(call proj1,$*) on host $(call get,$*)"
	@ssh -t -l $(SLICE) $(HOST_$(call get,$*)) \
		sudo ovs-vsctl set interface L$(call proj1,$*) \
			options:remote_ip=$$(cat cache/host.$(call rget,$*)) \
			options:remote_port=$$(cat cache/iface.$(call opp,$*)) \
	 && touch $@

