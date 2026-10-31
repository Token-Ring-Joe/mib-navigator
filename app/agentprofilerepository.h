#ifndef AGENTPROFILEREPOSITORY_H
#define AGENTPROFILEREPOSITORY_H

// Compatibility include only. The saved-profile repository (agents.conf) is retired; this
// header exists so files that still include it keep seeing AgentProfileRecord. Include
// "agentprofilerecord.h" instead; this shim is removed in a follow-up cleanup.
#include "agentprofilerecord.h"

#endif /* AGENTPROFILEREPOSITORY_H */
