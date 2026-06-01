add_library(gengodb 
    semantics/RdfGraph.cpp 
    catalog/CreateRdfGraphDef.cpp
    catalog/GraphCatalogEntry.cpp )
target_link_libraries(gengodb PRIVATE rdf4cpp::rdf4cpp LibXml2)