#!/usr/bin/env python3
"""Offline tool: computes golden answer sets for the real SPARQL query files
under resources/sparql/{coffee,bsbm}/ (including group subdirectories such as
coffee/agg/, coffee/path/, ...; `errors/` subdirectories are skipped), using
rdflib as an independent SPARQL engine, and writes them as JSON fixtures under
tools/gengodb/oracle/golden/ -- picked up automatically by
tools/gengodb/sparql-oracle.sh's result check (it looks for
golden/<path>.json, where <path> is the query's path relative to
resources/sparql/ with `/` replaced by `__`, e.g. coffee/agg/agg_sample.sparql
-> coffee__agg__agg_sample.sparql.json).

Unlike generate_golden.py (which runs hand-translated, hand-derived queries
against coffee.ttl for the GPM MLIR snippets), this runs the *actual*
.sparql query text run-sparql executes, unmodified, straight through rdflib.

- coffee/*.sparql wrap their pattern in
  `GRAPH <file://resources/ttl/coffee/coffee.ttl#rdf> { ... }`, so
  coffee.ttl is loaded into an rdflib Dataset under that exact graph IRI
  and the query is run against the dataset unmodified.
- bsbm/*.sparql have no GRAPH clause and query the default graph, so
  product.ttl is loaded into a plain rdflib.Graph.

bsbm/12.sparql (CONSTRUCT) is skipped: run-sparql rejects it outright
("not yet supported"), so there's nothing to check yet.

DESCRIBE queries are checked as a (subject, predicate, object) table: run-sparql
implements DESCRIBE as a rewrite to `SELECT DISTINCT ?subject ?predicate ?object`
returning only rule 1 of the Concise Bounded Description (the outgoing triples
of each described resource). rdflib computes the full CBD (rule 2: blank node
closure, rule 3: reifications), so `Graph.cbd` is replaced by a rule-1-only
version here; rdflib still decides *which* resources get described. rdflib 7.x
crashes on `DESCRIBE *`, so such a query must name the variables to use instead
in a `# oracle: describe-star ?a ?b ...` header line.

Queries using the GengoDB-specific fuzzy `TRUTH ?t OF [hedge] { ... }` extension
(e.g. coffee/misc/truth_crisp.sparql) are skipped too: rdflib can't parse that
non-standard construct, so there's no independent oracle for them.

This is NOT run as part of the build or test suite -- it requires `rdflib`
(`pip install rdflib`), and only needs to be re-run by hand if the ttl
fixtures or a query's text change.

Usage: python3 generate_sparql_golden.py
"""
import glob
import os
import re
import sys

try:
    import rdflib
except ImportError:
    sys.exit("generate_sparql_golden.py needs rdflib: pip install rdflib")

sys.path.insert(0, os.path.dirname(__file__))
import rdflib_golden as golden

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
GOLDEN_DIR = os.path.join(os.path.dirname(__file__), "golden")

COFFEE_GRAPH_IRI = "file://resources/ttl/coffee/coffee.ttl#rdf"
SKIP = {"bsbm/12.sparql"}  # CONSTRUCT not yet supported
# list of exclusions, due to rdflib issues
SKIP_RDFLIB = {"coffee/agg/agg_expr_arg_error.sparql", "coffee/agg/agg_group_by_expr_noalias.sparql",
               "coffee/agg/agg_computed_alias_chain.sparql"}
TRUTH_OF_RE = re.compile(r"\bTRUTH\s+\S+\s+OF\b", re.IGNORECASE)  # fuzzy extension, not standard SPARQL
DESCRIBE_STAR_RE = re.compile(r"^#\s*oracle:\s*describe-star((?:\s+\?\w+)+)\s*$", re.MULTILINE)
UNORDERED_CONCAT_RE = re.compile(r'^#\s*oracle:\s*unordered-concat\s+\?(\w+)\s+"((?:[^"\\]|\\.)*)"\s*$', re.MULTILINE)


def cbd_rule1(self, resource, *, target_graph=None, include_reifications=True):
    """Rule 1 of the Concise Bounded Description only: the triples with `resource` as subject.
    This is all run-sparql's DESCRIBE returns (no blank node closure, no reifications)."""
    subgraph = rdflib.Graph() if target_graph is None else target_graph
    for triple in self.triples((resource, None, None)):
        subgraph.add(triple)
    return subgraph


rdflib.Graph.cbd = cbd_rule1


def load_coffee():
    ds = rdflib.Dataset()
    g = ds.graph(rdflib.URIRef(COFFEE_GRAPH_IRI))
    g.parse(os.path.join(REPO, "resources/ttl/coffee/coffee.ttl"), format="turtle")
    return ds


def load_bsbm():
    g = rdflib.Graph()
    g.parse(os.path.join(REPO, "resources/ttl/bsbm/product.ttl"), format="turtle")
    return g


def main():
    stores = {"coffee": load_coffee(), "bsbm": load_bsbm()}

    for category, store in stores.items():
        paths = glob.glob(os.path.join(REPO, "resources/sparql", category, "**", "*.sparql"), recursive=True)
        for path in sorted(paths):
            rel = os.path.relpath(path, os.path.join(REPO, "resources/sparql")).replace(os.sep, "/")
            if "errors" in rel.split("/")[:-1]:
                continue
            if rel in SKIP:
                print(f"skip {rel} (CONSTRUCT/DESCRIBE, not a row-table result)")
                continue
            if rel in SKIP_RDFLIB:
                print(f"skip {rel} (rdflib bug, no independent oracle)")
                continue
            with open(path) as f:
                query_text = f.read()
            if TRUTH_OF_RE.search(query_text):
                print(f"skip {rel} (TRUTH OF extension, not standard SPARQL)")
                continue
            star = DESCRIBE_STAR_RE.search(query_text)
            if star:
                query_text, n = re.subn(r"^(\s*)DESCRIBE\s+\*", r"\1DESCRIBE" + star.group(1), query_text, count=1, flags=re.IGNORECASE | re.MULTILINE)
                if n != 1:
                    sys.exit(f"{rel}: describe-star directive, but no DESCRIBE * in the query")
            result = store.query(query_text)
            if result.type == "DESCRIBE":
                rows = [tuple(golden.term_key(t) for t in triple) for triple in result.graph]
                result_vars = ["subject", "predicate", "object"]
            else:
                rows = [tuple(golden.term_key(t) for t in row) for row in result]
                result_vars = [str(v) for v in result.vars]
            unordered = []
            for var, sep in UNORDERED_CONCAT_RE.findall(query_text):
                if var not in result_vars:
                    sys.exit(f"{rel}: unordered-concat directive names ?{var}, which is not projected")
                unordered.append([result_vars.index(var), sep.encode().decode("unicode_escape")])
            out_path = os.path.join(GOLDEN_DIR, rel.replace("/", "__") + ".json")
            n = golden.write_golden(out_path, rows, unordered or None)
            print(f"wrote {out_path} ({n} rows)")


if __name__ == "__main__":
    main()
