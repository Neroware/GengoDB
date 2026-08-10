add_library(gengodb
    semantics/Identifiers.cpp
    semantics/RdfGraph.cpp
    semantics/Datatypes.cpp
    catalog/CreateRdfGraphDef.cpp
    catalog/GraphCatalogEntry.cpp
    catalog/GraphNodeIndexCatalogEntry.cpp
    engine/SparqlEngine.cpp )
# engine/SparqlEngine.cpp calls into the query execution engine (runner) and
# the scheduler; runner already depends on gengodb (e.g. RDFGraphCatalogEntry)
# at the symbol level, so this is a genuine mutual dependency between the two
# static libraries - declare it so CMake orders/repeats the link line
# correctly instead of relying on link-line ordering by convention alone.
target_link_libraries(gengodb PRIVATE rdf4cpp::rdf4cpp LibXml2 runner scheduler)