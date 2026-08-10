add_library(gengodb
    semantics/Identifiers.cpp
    semantics/RdfGraph.cpp
    semantics/Datatypes.cpp
    catalog/CreateRdfGraphDef.cpp
    catalog/GraphCatalogEntry.cpp
    catalog/GraphNodeIndexCatalogEntry.cpp
    execution/SparqlEngine.cpp )
target_link_libraries(gengodb PRIVATE rdf4cpp::rdf4cpp LibXml2 runner scheduler)