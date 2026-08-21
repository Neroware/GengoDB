#ifndef GENGODB_CREATERDFGRAPHDEF_H
#define GENGODB_CREATERDFGRAPHDEF_H

#include "lingodb/utility/Serialization.h"
#include "gengodb/runtime/GengoDBGraph.h"
#include "gengodb/semantics/RdfFileFormat.h"

namespace gengodb::catalog {
using namespace lingodb;
struct CreateRdfGraphDef {
    std::string name;
    rdf4cpp::IRI iri;
    semantics::RDFFileFormat format;
    std::string sourceFileName;
    int32_t nodeCapacity = DEFAULT_NODE_CAPACITY;
    int32_t relCapacity = DEFAULT_REL_CAPACITY;
    int32_t propCapacity = DEFAULT_PROP_CAPACITY;

    void serialize(utility::Serializer& serializer) const;
    static CreateRdfGraphDef deserialize(utility::Deserializer& deserializer);
};
}

#endif // GENGODB_CREATERDFGRAPHDEF_H