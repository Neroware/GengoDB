<div align="center">
  <img src=".github/gengodb-logo.svg" height="50">
</div>

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://github.com/Neroware/GengoDB/blob/main/LICENSE)


# GengoDB
GengoDB is a fork of [LingoDB](https://www.lingo-db.com), a cutting-edge data processing system that leverages compiler technology to achieve unprecedented flexibility and extensibility without sacrificing performance. This project extends the existing relational model with RDF knowledge graphs and a SPARQL frontend, powered by a native property graph runtime and declarative sub-operators for universal graph state processing.

# Getting Started
We refer to LingoDB's [documentation website](https://www.lingo-db.com/docs/) for set-up instructions.
1. Set up the [LingoDB](https://www.lingo-db.com/docs/ForDevelopers/Dependencies) dependencies first.
2. Install [rdf4cpp 0.1.13](https://github.com/tentris/rdf4cpp/releases/tag/v0.1.13).
3. Use the `sparql <DB_DIR>` tool to load Turtle files into `db.lingodb` and run SPARQL queries interactively.
4. Alternatively, run a single SPARQL file non-interactively with `run-sparql <SPARQL_FILE> <DB_DIR>`.

We also provide a Dockerfile with all required dependencies:

1. Run `build.sh` to build the Docker image.
2. Run `eval "$(ssh-agent -s)" && ssh-add <SSH_KEY>` to set up SSH.
3. Run `run.sh` to launch a dev container.

GengoDB features a proper endpoint implementing the SPARQL protocol:

1. Run `sparql-endpoint <DB_DIR> [--host <ADDR>] [--port <PORT>] [--enable-cache]` to host a SPARQL endpoint.
2. Add the `--enable-cache` flag to enable the codegen cache of hot queries (experimental).

## Documentation
For LingoDB's documentation, please visit [the documentation website](https://www.lingo-db.com/docs/) based on [this github repo](https://github.com/lingo-db/lingo-db.github.io).

## Disclaimer
This project is an extension of LingoDB for scientific purposes. Its goal is to test how the sub-operator middleware can be applied to graph workflows.

## Work in progress
As of now, we currently run tests with the [Berlin SPARQL Benchmark (BSBM)](http://wbsg.informatik.uni-mannheim.de/bizer/berlinsparqlbenchmark/) to finally get proper numbers out!
The following is currently in our pipeline:
- **Hot query cache:** Since the BSBM specification forces us to run the benchmark via its test driver, compile times are quite significant for small(er) scale factors when reporting legitimate BSBM numbers, especially when compared to iterator-based engines like Jena's ARQ processor. From a scientific perspective, reporting and evaluating our measurements to support our claim of sub-operators for graph workflows is already sufficient at this point. However, for later applications and since the measurements of LingoDB always separate runtime measurements from compiler overhead, we decided that a query codegen cache would be a good compromise even though it is against LingoDB's design philosophy of gradually increasing compiler speed (see this [issue](https://github.com/lingo-db/lingo-db/issues/57)).
- **Triple pattern optimizations:** GengoDB treats (only) the triple pattern as a *true* relational operator within the GPM dialect. However, no optimization on the relational algebra layer is deployed at this point in time. Triple pattern variable bindings are solved through expensive join operations, which is essentially a *correct* but unoptimized query plan. In some basic cases, the triple pattern operator is replaceable just by simple selection operators, so an optimization pass would have to replace these by a simpler, faster query plan.
- **Aggregates:** GengoDB still lacks aggregates for SPARQL queries.


## Future work
The following future work is currently considered/planned:
- **Sub-Operator caching:** Since compiler overhead for small(er) queries is significant, an interesting idea would be to cache sub-operator codegen instead of the entire query (i.e. the operators). The compiler would decide at JIT time whether to just insert pre-compiled assembly or whether it needs to compile the sub-operator anew. Most of the time in query compilation is lost when lowering the many operations on the CF/Util layer to the STD/LLVM layer. This has become even worse due to the intense type-dependent branching caused by the `Variant` dialect, which steps in as the wrapper around XSD datatypes (RDF has no schema). Comparing compile times and runtimes of cached operators and sub-operators to execution plans without caching would be an interesting experiment to conduct.
- **FuzzyRDF:** There are mainly [theoretical works](https://ieeexplore.ieee.org/abstract/document/4630561/) on this concept extending RDFS/OWL semantics with fuzziness concepts. Generally, implementing and developing a formal model of fuzziness would be an interesting idea, especially if one can validate it in cases where one grounds Large-Language-Models with a Fuzzy-semantic database.
- **Deployment in social robot applications:** This revolves around the idea that social knowledge is imprecise, highly heterogeneous, and subjective. The hypothesis states that modern social robots equipped with LLMs grounded on social knowledge made accessible through fuzzy semantic databases might perform more natural interactions than in cases where either is not present.
- **Just-in-time SPARQL query expansion:** Backwards chaining suffices for basic RDFS reasoning. (Some) rules are directly applicable to SPARQL queries, meaning the LingoDB/GengoDB compiler would act as a reasoner, which mitigates overhead. For large datasets where computing the inference hull is infeasible, our hypothesis states that, compared to modern RDFS JIT reasoners, this approach could prove beneficial.