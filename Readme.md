<div align="center">
  <img src=".github/gengodb-logo.svg" height="50">
</div>

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://github.com/Neroware/GengoDB/blob/main/LICENSE)


# GengoDB
GengoDB is a fork of [LingoDB](https://www.lingo-db.com), a cutting-edge data processing system that leverages compiler technology to achieve unprecedented flexibility and extensibility without sacrificing performance. This project extends the existing relational model by RDF knowledge graphs and a SPARQL frontend, powered by a native property graph runtime and declarative sub-operators for universal graph state processing.

# Getting Started
We refer to LingoDB's [documentation website](https://www.lingo-db.com/docs/) for set-up instructions.
1. Set up the [LingoDB](https://www.lingo-db.com/docs/ForDevelopers/Dependencies) dependencies first.
2. Install [rdf4cpp 0.1.13](https://github.com/tentris/rdf4cpp/releases/tag/v0.1.13).
3. Use the `sparql <DB_DIR>` tool to load Turtle files into `db.lingodb` and run SPARQL queries interactively.
4. Alternatively, run a single SPARQL file non-interactively with `run-sparql <SPARQL_FILE> <DB_DIR>`.

We also provide a Dockerfile with all required dependencies:

1. Run `build.sh` to build the Docker image.
2. Run `eval "$(ssh-agent -s) && ssh-add <SSH_KEY>` to set up SSH.
3. Run `run.sh` to launch a dev container.

## Documentation
For LingoDB's documentation, please visit [the documentation website](https://www.lingo-db.com/docs/) based on [this github repo](https://github.com/lingo-db/lingo-db.github.io).

## Disclaimer
This project is an extension of LingoDB for scientific purposes. Its goal is to test how the sub-operator middleware can be applied to graph workflows.

## Work in progress
As of now, we work towards implementing all required functionality for the [Berlin SPARQL Benchmark (BSBM)](http://wbsg.informatik.uni-mannheim.de/bizer/berlinsparqlbenchmark/) to finally get proper numbers out! The following is currently in our pipeline:
- Making the BSBM queries runnable.
- Indexing of bound variables in triple patterns to avoid expensive joins.
- `Variant` dialect expressing variant typing for RDF term bindings and type inference (currently variables can only be bound to graph node refs).

## Future work
The following future work is currently considered/planned:
- FuzzyRDF frontend for imprecise semantics.
- Deployment in social robot applications grounding LLMs with imprecise social knowledge.
- Just-in-time SPARQL query expansion and backwards chaining for RDFS reasoning.