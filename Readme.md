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

## Work In Progress
- **Graph Indexing:** Enable fast index lookups through graph sub-operators.
- **Aggregates:** GengoDB still lacks aggregates for SPARQL queries.
- **Fuzzy RDF:** A fuzzy SPARQL frontend for GengoDB.

## Future Work In Social Robotics
This revolves around the idea that social knowledge is imprecise, highly heterogeneous, and subjective. The hypothesis states that social-cognitive robot systems can produce more natural interactions, if they are equipped with imprecise semantic knowledge on their social context.