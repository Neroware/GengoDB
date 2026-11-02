// RUN: mlir-db-opt %s -split-input-file -verify-diagnostics -mlir-print-local-scope | FileCheck %s

// endpoint of a bound edge
module {
    func.func @main() {
        %0 = gpm.relalg.named_graph column : @graphs::@g({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        %1 = gpm.relalg.triple_pattern %0 @graphs::@g(?{@vars::@y({type = !gpm.variable_binding})}, ?{@vars::@p({type = !gpm.variable_binding})}, ?{@vars::@o({type = !gpm.variable_binding})})
        %2 = gpm.relalg.node_expansion %1 @graphs::@g(?{@vars::@y} -> ?{@edges::@e({type = !gpm.variable_binding})}) anchor: outgoing
        //CHECK: %3 = gpm.relalg.edge_expansion %2 @graphs::@g(?{@edges::@e} -> ?{@nodes::@to({type = !gpm.variable_binding})}) anchor : to_node
        %3 = gpm.relalg.edge_expansion %2 @graphs::@g(?{@edges::@e} -> ?{@nodes::@to({type = !gpm.variable_binding})}) anchor: to_node
        return
    }
}

// -----

// label scan: start nodes of all edges with the predicate
module {
    func.func @main() {
        %0 = gpm.relalg.named_graph column : @graphs::@g({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        %1 = gpm.relalg.triple_pattern %0 @graphs::@g(?{@vars::@y({type = !gpm.variable_binding})}, ?{@vars::@p({type = !gpm.variable_binding})}, ?{@vars::@o({type = !gpm.variable_binding})})
        %2 = gpm.relalg.node_expansion %1 @graphs::@g(?{@vars::@y} -> ?{@edges::@e({type = !gpm.variable_binding})}) anchor: outgoing
        //CHECK: %3 = gpm.relalg.edge_expansion %2 @graphs::@g(id{"http://ex.org/pred"} -> ?{@nodes::@from({type = !gpm.variable_binding})}) anchor : from_node
        %3 = gpm.relalg.edge_expansion %2 @graphs::@g(id{"http://ex.org/pred"} -> ?{@nodes::@from({type = !gpm.variable_binding})}) anchor: from_node
        return
    }
}

// -----

module {
    func.func @main() {
        %0 = gpm.relalg.named_graph column : @graphs::@g({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        %1 = gpm.relalg.triple_pattern %0 @graphs::@g(?{@vars::@y({type = !gpm.variable_binding})}, ?{@vars::@p({type = !gpm.variable_binding})}, ?{@vars::@o({type = !gpm.variable_binding})})
        %2 = gpm.relalg.node_expansion %1 @graphs::@g(?{@vars::@y} -> ?{@edges::@e({type = !gpm.variable_binding})}) anchor: outgoing
        // expected-error @+1 {{anchor must be a bound variable reference or an identifier}}
        %3 = gpm.relalg.edge_expansion %2 @graphs::@g(?{@edges::@f({type = !gpm.variable_binding})} -> ?{@nodes::@to({type = !gpm.variable_binding})}) anchor: to_node
        return
    }
}

// -----

module {
    func.func @main() {
        %0 = gpm.relalg.named_graph column : @graphs::@g({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        %1 = gpm.relalg.triple_pattern %0 @graphs::@g(?{@vars::@y({type = !gpm.variable_binding})}, ?{@vars::@p({type = !gpm.variable_binding})}, ?{@vars::@o({type = !gpm.variable_binding})})
        %2 = gpm.relalg.node_expansion %1 @graphs::@g(?{@vars::@y} -> ?{@edges::@e({type = !gpm.variable_binding})}) anchor: outgoing
        // expected-error @+1 {{target must be a fresh variable definition}}
        %3 = gpm.relalg.edge_expansion %2 @graphs::@g(?{@edges::@e} -> ?{@vars::@y}) anchor: to_node
        return
    }
}
