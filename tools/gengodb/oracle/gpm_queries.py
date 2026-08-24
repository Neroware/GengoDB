"""Reference SPARQL translations of the GPM MLIR snippets in
test/gengodb/gpm/snippets/, used by generate_golden.py to compute golden
answers via rdflib against resources/ttl/coffee/coffee.ttl.

These are hand-derived from reading each .mlir file; see the comment on any
entry that needed a non-obvious reading of the GPM semantics.

`optional/optional_chained_anchor.2.mlir` is intentionally NOT listed here:
it exercises a GPM-specific choice (a mandatory pattern reusing a variable
that an earlier OPTIONAL may have left unbound requires that variable to
actually be bound -- unlike plain SPARQL, where reusing an unbound variable
in a later mandatory triple pattern just treats it as fresh/unconstrained).
That is a deliberate divergence from generic SPARQL var-scoping, not
something an SPARQL-based oracle can validate.
"""

EX = "PREFIX ex: <http://example.org/>\n"

QUERIES = {
"bgp/all.mlir": EX + "SELECT ?s ?p ?o WHERE { ?s ?p ?o }",

# gpm1 is built on gpm0's output stream and reuses @vars::@what -- a
# self-join requiring a second (who2) satisfying the same conditions for
# the same "what".
"bgp/bgp.mlir": EX + """
SELECT ?who ?what WHERE {
  ?who ex:drinks ?what . ?who ex:eats ex:sushi .
  ?who2 ex:drinks ?what . ?who2 ex:eats ex:sushi .
}""",

"bgp/bgp_implicit_join.mlir": EX + "SELECT ?what WHERE { ex:bob ex:eats ?what . ex:bob ex:drinks ?what }",

"bgp/bgp_nested.mlir": EX + "SELECT ?what ?drink ?temp WHERE { ex:bob ex:eats ?what . ex:bob ex:drinks ?drink . ?drink ex:temp ?temp }",

# `_{"somefood"}` is a GPM blank-node term: an existentially-scoped,
# non-projected variable matching any node kind (see BNodeTermAttr handling
# in GPMToSubOp.cpp), i.e. exactly a SPARQL non-distinguished variable.
"bgp/bind_bnode.mlir": EX + "SELECT ?who ?amount WHERE { ?who ex:eats ?food . ?food ex:portions ?amount }",

"bgp/bind_object.mlir": EX + "SELECT ?what WHERE { ex:bob ex:eats ?what . ex:bob ex:drinks ?what }",

"bgp/bind_pred.mlir": EX + "SELECT ?what WHERE { ex:bob ?what ex:coffee . ?what ex:type ex:relation }",

"bgp/bind_predicate_reused.mlir": EX + "SELECT ?p WHERE { ex:bob ?p ex:sushi . ex:bob ?p ex:miso }",

"bgp/bind_subj_and_pred.mlir": EX + "SELECT ?s ?p ?o WHERE { ?s ?p ?o . ?s ?p ex:coffee }",

"bgp/bind_subject.mlir": EX + "SELECT ?who WHERE { ?who ex:drinks ex:coffee . ?who ex:eats ex:sushi }",

"bgp/bnode.mlir": EX + "SELECT ?who WHERE { ?who ex:eats ?food }",

"bgp/double_all.mlir": EX + "SELECT ?s ?p ?o WHERE { ?s ?p ?o }",

# relalg.join hashes on who/who2 (not "what"), i.e. a real cross-style join:
# both sides independently produce 9 (who=bob,what) rows, so joining on
# who=who2=bob gives the full 9x9 cross product.
"bgp/join.mlir": EX + """
SELECT ?who ?who2 WHERE {
  ?who ex:drinks ?what . ?who ex:eats ex:sushi .
  ?who2 ex:drinks ?what2 . ?who2 ex:eats ex:sushi .
}""",

"bgp/multi.mlir": EX + "SELECT ?who ?age WHERE { ?who ex:eats ?what . ?who ex:age ?age . ?who ex:eats ex:sushi }",

"bgp/multi_single_bgp.mlir": EX + "SELECT ?who ?age WHERE { ?who ex:eats ?what . ?who ex:age ?age . ?who ex:eats ex:sushi }",

"bgp/object.mlir": EX + "SELECT ?what ?what2 WHERE { ex:bob ex:drinks ?what . ex:bob ex:eats ?what2 }",

"bgp/pred_first.mlir": EX + "SELECT ?who ?what WHERE { ?who ex:eats ?what }",

"bgp/predicate.mlir": EX + "SELECT ?what WHERE { ex:bob ?what ex:coffee }",

"bgp/self_resuse.mlir": EX + "SELECT ?x WHERE { ?x ex:asksForMore ?x }",

# who and who2 share no variable -> implicit cross join, same as SPARQL.
"bgp/subject.mlir": EX + "SELECT ?who ?who2 WHERE { ?who ex:drinks ex:coffee . ?who2 ex:eats ex:sushi }",

"filter/filter_arith_add.mlir": EX + "SELECT ?person ?age WHERE { ?person ex:age ?age . FILTER(?age + 1 > 30) }",

"filter/filter_arith_div_by_zero.mlir": EX + "SELECT ?person ?age WHERE { ?person ex:age ?age . FILTER(?age / 0 > 100) }",

"filter/filter_arith_mul_var_var.mlir": EX + """
SELECT ?person ?age ?size WHERE {
  ?person ex:age ?age . ?person ex:cup ?cup . ?cup ex:size ?size .
  FILTER(?age * ?size = 30)
}""",

"filter/filter_arith_negate.mlir": EX + "SELECT ?person ?age WHERE { ?person ex:age ?age . FILTER(-?age < 0) }",

"filter/filter_boolean.mlir": EX + "SELECT ?person ?age WHERE { ?person ex:age ?age . FILTER((?age > 10 && ?age < 100) || !(?age < 10)) }",

# {type = 1002} tags the constant as an IRI (gengodb::semantics::xsd::AnyIRI),
# not a plain string -- so the comparison is IRI-vs-node-identity, not
# string-vs-literal.
"filter/filter_iri_eq.mlir": EX + "SELECT ?drink WHERE { ex:bob ex:drinks ?drink . FILTER(<http://example.org/coffee> = ?drink) }",

"filter/filter_iri_neq.mlir": EX + "SELECT ?drink WHERE { ex:bob ex:drinks ?drink . FILTER(<http://example.org/coffee> != ?drink) }",

"filter/filter_literal_gt.mlir": EX + "SELECT ?person ?food ?age WHERE { ?person ex:eats ?food . ?person ex:age ?age . FILTER(?age > 25) }",

"filter/filter_literal_le_excludes.mlir": EX + "SELECT ?person ?age WHERE { ?person ex:age ?age . FILTER(?age <= 25) }",

"filter/limit_basic.mlir": EX + "SELECT ?who ?what WHERE { ?who ex:drinks ?what } LIMIT 5",

"optional/optional_bound.mlir": EX + "SELECT ?person ?food ?cup WHERE { ?person ex:eats ?food . OPTIONAL { ?person ex:cup ?cup } }",

"optional/optional_chained_anchor.mlir": EX + """
SELECT ?person ?food ?cup ?drink WHERE {
  ?person ex:eats ?food .
  OPTIONAL { ?person ex:cup ?cup }
  OPTIONAL { ?cup ex:drinks ?drink }
}""",

"optional/optional_filter_unbound.mlir": EX + """
SELECT ?who ?what WHERE {
  ?who ex:eats ?what .
  OPTIONAL { ?who ex:age ?age }
  FILTER(?age > 20)
}""",

"optional/optional_multi.mlir": EX + """
SELECT ?who ?age WHERE {
  ?who ex:eats ?what .
  OPTIONAL { ?who ex:age ?age . ?who ex:eats ex:sushi }
}""",

"optional/optional_nested.mlir": EX + "SELECT ?who ?age WHERE { ?who ex:eats ?what . OPTIONAL { ?who ex:age ?age } }",

"optional/optional_nested_bgp.mlir": EX + """
SELECT ?drink ?t ?s WHERE {
  ex:bob ex:drinks ?drink .
  OPTIONAL { ?drink ex:temp ?t . ?drink ex:strong ?s }
}""",

"optional/optional_nested_multi.mlir": EX + """
SELECT ?who ?age WHERE {
  ?who ex:eats ?what .
  OPTIONAL { ?who ex:age ?age . ?who ex:eats ex:foo }
}""",

"optional/optional_nested_optional.mlir": EX + """
SELECT ?drink ?t ?s WHERE {
  ex:bob ex:drinks ?drink .
  OPTIONAL {
     ?drink ex:temp ?t .
     OPTIONAL { ?drink ex:strong ?s }
  }
}""",

"optional/optional_single.mlir": EX + "SELECT ?who ?age WHERE { ?who ex:eats ?what . OPTIONAL{?who ex:age ?age} }",

"optional/optional_unbound_key_join.mlir": EX + """
SELECT ?who ?what ?age ?who2 ?what2 WHERE {
  ?who ex:eats ?what .
  OPTIONAL { ?who ex:age ?age }
  ?who2 ex:eats ?what2 .
  OPTIONAL { ?who2 ex:age ?age }
}""",

"union/union_basic.mlir": EX + """
SELECT ?who ?what {
    { ?who ex:drinks ?what }
    UNION
    { ?who ex:eats ?what }
}""",

"union/union_disjoint_var.mlir": EX + """
SELECT ?who ?what ?age {
    { ?who ex:drinks ?what }
    UNION
    { ?who ex:age ?age }
}""",

"union/union_nested.mlir": EX + """
SELECT ?who ?what {
    { ?who ex:drinks ?what }
    UNION
    { ?who ex:eats ?what }
    UNION
    { ?who ex:age ?what }
}""",

"union/union_optional.mlir": EX + """
SELECT ?who ?what ?age {
    ?who ex:eats ex:sushi .
    OPTIONAL {
        { ?who ex:drinks ?what }
        UNION
        { ?who ex:age ?age }
    }
}""",

"union/union_pre_bound_var.mlir": EX + """
SELECT ?who ?what {
    ?who ex:eats ex:sushi .
    { ?who ex:drinks ?what }
    UNION
    { ?who ex:age ?what }
}""",
}
