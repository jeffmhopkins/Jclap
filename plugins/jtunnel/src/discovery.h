// Finds tunnels on the network with mDNS / DNS-SD (the same multicast group Bonjour uses, which
// Wi-Fi access points pass even when they filter other multicast), as service type
// _jtunnel._udp.local. One per process, shared by every JTunnel instance in it, with one thread.
//
// Senders publish "<tunnel name>._jtunnel._udp.local." with their port. Instances in the same
// process also see each other directly, without the network, so two tracks in one DAW work even
// where multicast is blocked. Receivers take a tunnel's address from where its announcement came
// from.
//
// A name belongs to whoever had it first. A new sender listens for a few seconds before it
// announces, and doesn't announce at all if someone else already has the name: it waits, and takes
// the name over when the other one goes. Two that announce at once (two projects opened together)
// settle it by when each started claiming the name, carried in a TXT record.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "net.h"

namespace jtunnel {

struct FoundTunnel {
   std::string name;
   net::Address address;
   bool local = false; // in this process
};

class Discovery {
public:
   // The shared instance, started by the first user and stopped when the last releases it.
   static Discovery &acquire();
   static void release();
   // The shared instance without changing who holds it, for code that already acquired it.
   static Discovery &instance();

   // Owner ids are per JTunnel instance; one owner publishes at most one tunnel.
   void publish(uint64_t owner, const std::string &name, uint16_t port);
   void unpublish(uint64_t owner);
   // True while the owner's name is in use by another sender, so it isn't announced; `by` says
   // where ("192.168.1.31:47000", or "another track here").
   bool nameTaken(uint64_t owner, std::string &by) const;

   // Every tunnel seen recently, local ones first, sorted by name within each.
   std::vector<FoundTunnel> tunnels() const;
   // The address for a tunnel name (case-insensitive), preferring one in this process.
   bool resolve(const std::string &name, net::Address &out) const;
   // False when the mDNS socket couldn't be opened: only tunnels in this process can be found.
   bool networkAvailable() const;

   // An id for a new owner.
   static uint64_t newOwnerId();

   struct Impl;

private:
   Discovery() = default;
};

} // namespace jtunnel
