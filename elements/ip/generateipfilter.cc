// -*- c-basic-offset: 4; related-file-name: "generateipfilter.hh" -*-
/*
 * GenerateIPFilter.{cc,hh} -- element generates IPFilter patterns out of input traffic
 * Tom Barbette, (extended by) Georgios Katsikas
 *
 * Copyright (c) 2017 Tom Barbette, University of Liège
 * Copyright (c) 2017 Georgios Katsikas, RISE SICS
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, subject to the conditions
 * listed in the Click LICENSE file. These conditions include: you must
 * preserve this copyright notice, and you cannot mention the copyright
 * holders in advertising related to the Software without their permission.
 * The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
 * notice is a summary of the Click LICENSE file; the license in that file is
 * legally binding.
 */

#include <click/config.h>
#include <click/glue.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <click/straccum.hh>

#include "generateipfilter.hh"

CLICK_DECLS

const int GenerateIPPacket::DEF_NB_RULES = 8000;

/**
 * Base class for pattern generation out of incoming traffic.
 */
GenerateIPPacket::GenerateIPPacket() : _nrules(DEF_NB_RULES)
{
}

GenerateIPPacket::~GenerateIPPacket()
{
}

int
GenerateIPPacket::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
            .read_p("NB_RULES", _nrules)
            .consume() < 0)
        return -1;

  return 0;
}

int
GenerateIPPacket::initialize(ErrorHandler *errh)
{
    return 0;
}

void
GenerateIPPacket::cleanup(CleanupStage)
{
    return;
}

/**
 * IP FIlter rules' generator out of incoming traffic.
 */
GenerateIPFilter::GenerateIPFilter() : GenerateIPPacket(), _keep_sport(false), _keep_dport(true)
{
}

GenerateIPFilter::~GenerateIPFilter()
{
}

int
GenerateIPFilter::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
            .read("KEEP_SPORT", _keep_sport)
            .read("KEEP_DPORT", _keep_dport)
            .consume() < 0)
        return -1;

    _mask = IPFlowID(0xffffffff, (_keep_sport?0xffff:0), 0xffffffff, (_keep_dport?0xffff:0));

    return GenerateIPPacket::configure(conf, errh);
}

int
GenerateIPFilter::initialize(ErrorHandler *errh)
{
    return GenerateIPPacket::initialize(errh);
}

void
GenerateIPFilter::cleanup(CleanupStage)
{
}

Packet *
GenerateIPFilter::simple_action(Packet *p)
{
    IPFlowID flowid(p);
    IPFlow flow = IPFlow();
    flow.initialize(flowid & _mask);
    _map.find_insert(flow);

    return p;
}

#if HAVE_BATCH
PacketBatch*
GenerateIPFilter::simple_action_batch(PacketBatch *batch)
{
    EXECUTE_FOR_EACH_PACKET(simple_action, batch);
    return batch;
}
#endif

String
GenerateIPFilter::read_handler(Element *e, void *user_data)
{
    GenerateIPFilter *g = static_cast<GenerateIPFilter *>(e);
    if (!g) {
        return "GenerateIPFilter element not found";
    }

    StringAccum acc;

    int n = 0;
    while (g->_map.size() > g->_nrules) {
        HashTable<IPFlow> newmap;
        ++n;
        g->_mask = IPFlowID(
            IPAddress::make_prefix(32 - n), g->_mask.sport(),
            IPAddress::make_prefix(32 - n), g->_mask.dport()
        );
        for (auto flow : g->_map) {
            flow.setMask(g->_mask);
            newmap.find_insert(flow);
        }
        g->_map = newmap;
        if (n == 32) {
            return "Impossible to lower the rules and keep the choosen fields";
        }
    }
    for (auto flow : g->_map) {
        acc << "allow src net " << flow.flowid().saddr() << '/' << String(32-n) << " && "
                 << " dst net " << flow.flowid().daddr() << '/' << String(32-n);
        if (g->_keep_sport)
            acc << " && src port " << flow.flowid().sport();
        if (g->_keep_dport) {
            acc << " && dst port " << flow.flowid().dport();
        }
        acc << ",\n";
    }

    return acc.take_string();
}

void
GenerateIPFilter::add_handlers()
{
    add_read_handler("dump", read_handler);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(GenerateIPFilter)
