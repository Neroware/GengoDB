#include "catch2/catch_all.hpp"
#include "lingodb/catalog/Defs.h"
#include "lingodb/catalog/IndexCatalogEntry.h"
#include "lingodb/catalog/TableCatalogEntry.h"
#include "lingodb/catalog/Types.h"
#include "lingodb/utility/Serialization.h"

#include "gengodb/catalog/GraphCatalogEntry.h"
#include "gengodb/catalog/GraphNodeIndexCatalogEntry.h"
#include "gengodb/semantics/RdfGraph.h"

#include <arrow/builder.h>
#include <arrow/ipc/reader.h>
#include <lingodb/catalog/Column.h>
#include <lingodb/catalog/MetaData.h>
#include <unordered_set>
using namespace lingodb::utility;
using namespace lingodb::catalog;

TEST_CASE("TableCatalogEntry:CreateTableDef") {
   CreateTableDef createTableDef;
   createTableDef.name = "test_table";
   createTableDef.columns = {Column("col1", Type::int8(), true), Column("col2", Type::stringType(), false)};
   createTableDef.primaryKey = {"col1"};

   SimpleByteWriter writer;
   Serializer serializer(writer);
   serializer.writeProperty(1, createTableDef);

   SimpleByteReader reader(writer.data(), writer.size());
   Deserializer deserializer(reader);
   auto createTableDef2 = deserializer.readProperty<CreateTableDef>(1);

   REQUIRE(createTableDef.name == createTableDef2.name);
   REQUIRE(createTableDef.columns.size() == createTableDef2.columns.size());
   REQUIRE(createTableDef.primaryKey == createTableDef2.primaryKey);
}

TEST_CASE("TableCatalogEntry:CreateAndSerialize") {
   CreateTableDef createTableDef;
   createTableDef.name = "test_table";
   createTableDef.columns = {Column("col1", Type::int8(), true), Column("col2", Type::stringType(), false)};
   createTableDef.primaryKey = {"col1"};
   auto tableEntry = LingoDBTableCatalogEntry::createFromCreateTable(createTableDef);
   REQUIRE(tableEntry->getName() == "test_table");
   REQUIRE(tableEntry->getColumns().size() == 2);
   REQUIRE(tableEntry->getColumns()[0].getColumnName() == "col1");
   REQUIRE(tableEntry->getColumns()[0].getLogicalType().toString() == "int8");
   REQUIRE(tableEntry->getColumns()[1].getColumnName() == "col2");
   REQUIRE(tableEntry->getColumns()[1].getLogicalType().toString() == "string");
   REQUIRE(tableEntry->getPrimaryKey().size() == 1);
   REQUIRE(tableEntry->getPrimaryKey()[0] == "col1");
   SimpleByteWriter writer;
   Serializer serializer(writer);
   serializer.writeProperty(1, std::dynamic_pointer_cast<CatalogEntry>(tableEntry));
   SimpleByteReader reader(writer.data(), writer.size());
   Deserializer deserializer(reader);
   auto catalogEntry = deserializer.readProperty<std::shared_ptr<CatalogEntry>>(1);
   REQUIRE(catalogEntry->getEntryType() == CatalogEntry::CatalogEntryType::LINGODB_TABLE_ENTRY);
   auto tableEntry2 = std::dynamic_pointer_cast<LingoDBTableCatalogEntry>(catalogEntry);
   REQUIRE(tableEntry2 != nullptr);
   REQUIRE(tableEntry2->getName() == "test_table");
   REQUIRE(tableEntry2->getColumns().size() == 2);
   REQUIRE(tableEntry2->getColumns()[0].getColumnName() == "col1");
   REQUIRE(tableEntry2->getColumns()[0].getLogicalType().toString() == "int8");
   REQUIRE(tableEntry2->getColumns()[1].getColumnName() == "col2");
   REQUIRE(tableEntry2->getColumns()[1].getLogicalType().toString() == "string");
   REQUIRE(tableEntry2->getPrimaryKey().size() == 1);
   REQUIRE(tableEntry2->getPrimaryKey()[0] == "col1");
}

TEST_CASE("IndexCatalogEntry:CreateAndSerialize") {
   auto indexEntry = LingoDBHashIndexEntry::createForPrimaryKey("test_table", {"col1"});
   REQUIRE(indexEntry->getName() == "test_table.pk");
   REQUIRE(indexEntry->getTableName() == "test_table");
   REQUIRE(indexEntry->getIndexedColumns().size() == 1);
   REQUIRE(indexEntry->getIndexedColumns()[0] == "col1");
   SimpleByteWriter writer;
   Serializer serializer(writer);
   serializer.writeProperty(1, std::dynamic_pointer_cast<CatalogEntry>(indexEntry));
   SimpleByteReader reader(writer.data(), writer.size());
   Deserializer deserializer(reader);
   auto catalogEntry = deserializer.readProperty<std::shared_ptr<CatalogEntry>>(1);
   REQUIRE(catalogEntry->getEntryType() == CatalogEntry::CatalogEntryType::LINGODB_HASH_INDEX_ENTRY);
   auto indexEntry2 = std::dynamic_pointer_cast<LingoDBHashIndexEntry>(catalogEntry);
   REQUIRE(indexEntry2 != nullptr);
   REQUIRE(indexEntry2->getName() == "test_table.pk");
   REQUIRE(indexEntry2->getTableName() == "test_table");
   REQUIRE(indexEntry2->getIndexedColumns().size() == 1);
   REQUIRE(indexEntry2->getIndexedColumns()[0] == "col1");
}

TEST_CASE("GraphNodeIndexCatalogEntry:BuildAndSerialize") {
   using namespace gengodb::catalog;
   using namespace gengodb::semantics;
   using namespace rdf4cpp;

   const IRI pIri{"http://example.org/p"};
   const IRI hasNameIri{"http://example.org/hasName"};
   const IRI xsdString{"http://www.w3.org/2001/XMLSchema#string"};
   const Literal sharedLiteral = Literal::make_typed("shared-literal", xsdString);
   const BlankNode sharedLabelBNode{"shared-label"};

   const IRI g1Iri{"http://example.org/g1"};
   auto rdf1 = RdfGraph::create("g1", g1Iri);
   rdf1->addTriple(IRI{"http://example.org/s1"}, pIri, IRI{"http://example.org/o1"});
   rdf1->addTriple(IRI{"http://example.org/s1"}, hasNameIri, sharedLiteral);
   rdf1->addTriple(sharedLabelBNode, pIri, IRI{"http://example.org/o1"});
   auto g1 = std::make_shared<RDFGraphCatalogEntry>("g1", std::move(rdf1), RDFFileFormat::TURTLE);

   const IRI g2Iri{"http://example.org/g2"};
   auto rdf2 = RdfGraph::create("g2", g2Iri);
   rdf2->addTriple(IRI{"http://example.org/s2a"}, pIri, IRI{"http://example.org/o2"});
   rdf2->addTriple(IRI{"http://example.org/s2b"}, pIri, IRI{"http://example.org/o2"});
   rdf2->addTriple(IRI{"http://example.org/s2a"}, hasNameIri, sharedLiteral);
   rdf2->addTriple(sharedLabelBNode, pIri, IRI{"http://example.org/o2"});
   auto g2 = std::make_shared<RDFGraphCatalogEntry>("g2", std::move(rdf2), RDFFileFormat::TURTLE);

   const std::string g1Name{g1Iri.identifier()};
   const std::string g2Name{g2Iri.identifier()};
   const int32_t n1 = static_cast<int32_t>(g1->getNodes().size());
   const int32_t n2 = static_cast<int32_t>(g2->getNodes().size());

   auto indexEntry = GraphNodeIndexCatalogEntry::build({std::make_pair(std::string{}, &g1->getGraph()), std::make_pair(std::string{}, &g2->getGraph())});
   REQUIRE(indexEntry->getName() == GraphNodeIndexCatalogEntry::ENTRY_NAME);

   for (int32_t localId = 0; localId < n1; localId++) {
      int64_t globalId = indexEntry->getGlobalId(g1Name, localId);
      REQUIRE(globalId >= 0);
      REQUIRE(indexEntry->getLocalId(g1Name, globalId) == localId);
   }
   for (int32_t localId = 0; localId < n2; localId++) {
      int64_t globalId = indexEntry->getGlobalId(g2Name, localId);
      REQUIRE(globalId >= 0);
      REQUIRE(indexEntry->getLocalId(g2Name, globalId) == localId);
   }
   
   auto findLocalId = [&](const std::shared_ptr<RDFGraphCatalogEntry>& g, const Node& node) -> int32_t {
      if (node.is_literal()) {
         const auto& dict = g->getNodes();
         for (int32_t id = 0; id < static_cast<int32_t>(dict.size()); id++) {
            if (dict.get(id).type == RDFNodeType::Literal && g->getNode(id) == node) {
               return id;
            }
         }
         return -1;
      }
      return g->getNodes().get_safe(node);
   };
   auto globalIdOf = [&](const std::shared_ptr<RDFGraphCatalogEntry>& g, const std::string& gName, const Node& node) -> int64_t {
      int32_t localId = findLocalId(g, node);
      REQUIRE(localId >= 0);
      return indexEntry->getGlobalId(gName, localId);
   };

   // The same IRI appearing in two different graphs must resolve to the SAME
   // global id: this is the semantic matching the index exists to provide.
   REQUIRE(globalIdOf(g1, g1Name, pIri) == globalIdOf(g2, g2Name, pIri));
   REQUIRE(globalIdOf(g1, g1Name, hasNameIri) == globalIdOf(g2, g2Name, hasNameIri));

   // The same literal (identical content + datatype) appearing in two different
   // graphs must also resolve to the SAME global id.
   REQUIRE(globalIdOf(g1, g1Name, sharedLiteral) == globalIdOf(g2, g2Name, sharedLiteral));

   // Blank nodes are graph-local per the RDF spec: an identical label in two
   // different graphs must NOT be treated as the same entity.
   REQUIRE(globalIdOf(g1, g1Name, sharedLabelBNode) != globalIdOf(g2, g2Name, sharedLabelBNode));

   // IRIs/literals that only ever appear in one graph must not collide with
   // anything else (dedup must not be over-eager).
   std::unordered_set<int64_t> distinctOnly = {
      globalIdOf(g1, g1Name, IRI{"http://example.org/s1"}),
      globalIdOf(g1, g1Name, IRI{"http://example.org/o1"}),
      globalIdOf(g2, g2Name, IRI{"http://example.org/s2a"}),
      globalIdOf(g2, g2Name, IRI{"http://example.org/s2b"}),
      globalIdOf(g2, g2Name, IRI{"http://example.org/o2"}),
   };
   REQUIRE(distinctOnly.size() == 5);

   // not-found lookups return -1
   REQUIRE(indexEntry->getGlobalId(g1Name, 999) == -1);
   REQUIRE(indexEntry->getLocalId(g1Name, 999999) == -1);

   // round-trip through the LingoDB catalog (de)serialization machinery preserves
   // every mapping exactly, including the deduplicated ones.
   SimpleByteWriter writer;
   Serializer serializer(writer);
   serializer.writeProperty(1, std::dynamic_pointer_cast<CatalogEntry>(indexEntry));
   SimpleByteReader reader(writer.data(), writer.size());
   Deserializer deserializer(reader);
   auto catalogEntry = deserializer.readProperty<std::shared_ptr<CatalogEntry>>(1);
   REQUIRE(catalogEntry->getEntryType() == CatalogEntry::CatalogEntryType::GENGODB_NODE_INDEX_ENTRY);
   auto indexEntry2 = std::dynamic_pointer_cast<GraphNodeIndexCatalogEntry>(catalogEntry);
   REQUIRE(indexEntry2 != nullptr);
   for (int32_t localId = 0; localId < n1; localId++) {
      REQUIRE(indexEntry2->getGlobalId(g1Name, localId) == indexEntry->getGlobalId(g1Name, localId));
   }
   for (int32_t localId = 0; localId < n2; localId++) {
      REQUIRE(indexEntry2->getGlobalId(g2Name, localId) == indexEntry->getGlobalId(g2Name, localId));
   }
}