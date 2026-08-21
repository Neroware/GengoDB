#include "gengodb/catalog/CreateRdfGraphDef.h"

void gengodb::catalog::CreateRdfGraphDef::serialize(utility::Serializer& serializer) const {
   serializer.writeProperty(1, name);
   serializer.writeProperty(2, iri.identifier());
   serializer.writeProperty(3, (int) format);
   serializer.writeProperty(4, sourceFileName);
   serializer.writeProperty(5, nodeCapacity);
   serializer.writeProperty(6, relCapacity);
   serializer.writeProperty(7, propCapacity);
}
gengodb::catalog::CreateRdfGraphDef gengodb::catalog::CreateRdfGraphDef::deserialize(utility::Deserializer& deserializer) {
   auto name = deserializer.readProperty<std::string>(1);
   auto iri = deserializer.readProperty<std::string>(2);
   auto format = deserializer.readProperty<int>(3);
   auto sourceFileName = deserializer.readProperty<std::string>(4);
   auto nodeCapacity = deserializer.readProperty<int32_t>(5);
   auto relCapacity = deserializer.readProperty<int32_t>(6);
   auto propCapacity = deserializer.readProperty<int32_t>(7);
   return CreateRdfGraphDef{name, rdf4cpp::IRI{iri}, (semantics::RDFFileFormat) format, sourceFileName, nodeCapacity, relCapacity, propCapacity};
}
