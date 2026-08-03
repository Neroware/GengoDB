add_library(gengodb Foo.cpp)
    #semantics/Identifiers.cpp 
    #semantics/RdfGraph.cpp
    #semantics/Datatypes.cpp
    #catalog/CreateRdfGraphDef.cpp
    #catalog/GraphCatalogEntry.cpp )
target_link_libraries(gengodb PRIVATE rdf4cpp::rdf4cpp LibXml2)