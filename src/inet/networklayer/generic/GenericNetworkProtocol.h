//
// Copyright (C) 2012 Opensim Ltd.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
//

#ifndef __INET_GENERICNETWORKPROTOCOL_H
#define __INET_GENERICNETWORKPROTOCOL_H

#include <list>
#include <map>

#include "inet/common/packet/Packet.h"
#include "inet/common/IProtocolRegistrationListener.h"
#include "inet/networklayer/contract/IARP.h"
#include "inet/networklayer/contract/INetworkProtocol.h"
#include "inet/common/queue/QueueBase.h"
#include "inet/networklayer/contract/IInterfaceTable.h"
#include "inet/networklayer/contract/INetfilter.h"
#include "inet/networklayer/generic/GenericRoutingTable.h"
#include "inet/networklayer/generic/GenericDatagram.h"
#include "inet/common/ProtocolMap.h"

namespace inet {

/**
 * Implements a generic network protocol that routes generic datagrams through the network.
 * Routing decisions are based on a generic routing table, but it also supports the netfilter
 * interface to allow routing protocols to kick in. It doesn't provide datagram fragmentation
 * and reassembling.
 */
// TODO: rename this and its friends to something that is more specific
// TODO: that expresses to some extent how this network protocol works
class INET_API GenericNetworkProtocol : public QueueBase, public NetfilterBase, public INetworkProtocol, public IProtocolRegistrationListener
{
  protected:
    /**
     * Represents an GenericDatagram, queued by a Hook
     */
    struct QueuedDatagramForHook
    {
      public:
        QueuedDatagramForHook(Packet *datagram, const InterfaceEntry *inIE, const InterfaceEntry *outIE,
                L3Address& nextHop, INetfilter::IHook::Type hookType)
            : datagram(datagram), inIE(inIE), outIE(outIE), nextHop(nextHop), hookType(hookType) {}

        virtual ~QueuedDatagramForHook() {}

        Packet *datagram;
        const InterfaceEntry *inIE;
        const InterfaceEntry *outIE;
        const L3Address nextHop;
        const INetfilter::IHook::Type hookType;
    };

    struct SocketDescriptor
    {
        int socketId = -1;
        int protocolId = -1;

        SocketDescriptor(int socketId, int protocolId) : socketId(socketId), protocolId(protocolId) { }
    };

    IInterfaceTable *interfaceTable;
    GenericRoutingTable *routingTable;
    IARP *arp;

    // config
    int defaultHopLimit;

    // working vars
    ProtocolMapping mapping;    // where to send packets after decapsulation
    std::map<int, SocketDescriptor *> socketIdToSocketDescriptor;
    std::multimap<int, SocketDescriptor *> protocolIdToSocketDescriptors;

    // hooks
    typedef std::list<QueuedDatagramForHook> DatagramQueueForHooks;
    DatagramQueueForHooks queuedDatagramsForHooks;

    // statistics
    int numLocalDeliver;
    int numDropped;
    int numUnroutable;
    int numForwarded;

  protected:
    // utility: look up interface from getArrivalGate()
    virtual const InterfaceEntry *getSourceInterfaceFrom(cPacket *packet);

    // utility: show current statistics above the icon
    virtual void refreshDisplay() const override;

    /**
     * Handle GenericDatagram messages arriving from lower layer.
     * Decrements TTL, then invokes routePacket().
     */
    virtual void handlePacketFromNetwork(Packet *datagram);

    /**
     * Handle packets from transport or ICMP.
     * Invokes encapsulate(), then routePacket().
     */
    virtual void handlePacketFromHL(Packet *packet);

    /**
     * Performs routing. Based on the routing decision, it dispatches to
     * sendDatagramToHL() for local packets, to sendDatagramToOutput() for forwarded packets,
     * to handleMulticastPacket() for multicast packets, or drops the packet if
     * it's unroutable or forwarding is off.
     */
    virtual void routePacket(Packet *datagram, const InterfaceEntry *destIE, const L3Address& nextHop, bool fromHL);

    /**
     * Forwards packets to all multicast destinations, using sendDatagramToOutput().
     */
    virtual void routeMulticastPacket(Packet *datagram, const InterfaceEntry *destIE, const InterfaceEntry *fromIE);

    /**
     * Encapsulate packet coming from higher layers into GenericDatagram, using
     * the control info attached to the packet.
     */
    virtual void encapsulate(Packet *transportPacket, const InterfaceEntry *& destIE);

    /**
     * Decapsulate and return encapsulated packet.
     */
    virtual void decapsulate(Packet *datagram);

    /**
     * Send datagrams up to the higher layers.
     */
    virtual void sendDatagramToHL(Packet *datagram);

    /**
     * Last TTL check, then send datagram on the given interface.
     */
    virtual void sendDatagramToOutput(Packet *datagram, const InterfaceEntry *ie, L3Address nextHop);

    virtual void datagramPreRouting(Packet *datagram, const InterfaceEntry *inIE, const InterfaceEntry *destIE, const L3Address& nextHop);
    virtual void datagramLocalIn(Packet *datagram, const InterfaceEntry *inIE);
    virtual void datagramLocalOut(Packet *datagram, const InterfaceEntry *destIE, const L3Address& nextHop);

    virtual IHook::Result datagramPreRoutingHook(Packet *datagram, const InterfaceEntry *inIE, const InterfaceEntry *& outIE, L3Address& nextHop);
    virtual IHook::Result datagramForwardHook(Packet *datagram, const InterfaceEntry *inIE, const InterfaceEntry *& outIE, L3Address& nextHop);
    virtual IHook::Result datagramPostRoutingHook(Packet *datagram, const InterfaceEntry *inIE, const InterfaceEntry *& outIE, L3Address& nextHop);
    virtual IHook::Result datagramLocalInHook(Packet *datagram, const InterfaceEntry *inIE);
    virtual IHook::Result datagramLocalOutHook(Packet *datagram, const InterfaceEntry *& outIE, L3Address& nextHop);

  public:
    GenericNetworkProtocol();
    ~GenericNetworkProtocol();

    virtual void handleRegisterProtocol(const Protocol& protocol, cGate *gate) override;

    virtual void registerHook(int priority, IHook *hook) override;
    virtual void unregisterHook(IHook *hook) override;
    virtual void dropQueuedDatagram(const Packet *datagram) override;
    virtual void reinjectQueuedDatagram(const Packet *datagram) override;

  protected:
    /**
     * Initialization
     */
    virtual void initialize(int stage) override;
    virtual int numInitStages() const override { return NUM_INIT_STAGES; }

    virtual void handleMessage(cMessage *msg) override;
    void handleCommand(cMessage *msg);

    /**
     * Processing of generic datagrams. Called when a datagram reaches the front
     * of the queue.
     */
    virtual void endService(cPacket *packet) override;
};

} // namespace inet

#endif // ifndef __INET_GENERICNETWORKPROTOCOL_H

