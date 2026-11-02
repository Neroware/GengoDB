// RUN: mlir-db-opt %s -split-input-file -verify-diagnostics -mlir-print-local-scope | FileCheck %s

// outgoing edges of a bound node
module {
    func.func @main() {
        %0 = gpm.relalg.named_graph column : @graphs::@g({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        %1 = gpm.relalg.triple_pattern %0 @graphs::@g(?{@vars::@y({type = !gpm.variable_binding})}, ?{@vars::@p({type = !gpm.variable_binding})}, ?{@vars::@o({type = !gpm.variable_binding})})
        //CHECK: %2 = gpm.relalg.node_expansion %1 @graphs::@g(?{@vars::@y} -> ?{@edges::@e({type = !gpm.variable_binding})}) anchor : outgoing
        %2 = gpm.relalg.node_expansion %1 @graphs::@g(?{@vars::@y} -> ?{@edges::@e({type = !gpm.variable_binding})}) anchor: outgoing
        return
    }
}

// -----

// incoming edges of a constant node
module {
    func.func @main() {
        %0 = gpm.relalg.named_graph column : @graphs::@g({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        %1 = gpm.relalg.triple_pattern %0 @graphs::@g(?{@vars::@y({type = !gpm.variable_binding})}, ?{@vars::@p({type = !gpm.variable_binding})}, ?{@vars::@o({type = !gpm.variable_binding})})
        //CHECK: %2 = gpm.relalg.node_expansion %1 @graphs::@g(id{"http://ex.org/foo"} -> ?{@edges::@e({type = !gpm.variable_binding})}) anchor : incoming
        %2 = gpm.relalg.node_expansion %1 @graphs::@g(id{"http://ex.org/foo"} -> ?{@edges::@e({type = !gpm.variable_binding})}) anchor: incoming
        return
    }
}

// -----

module {
    func.func @main() {
        %0 = gpm.relalg.named_graph column : @graphs::@g({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        %1 = gpm.relalg.triple_pattern %0 @graphs::@g(?{@vars::@y({type = !gpm.variable_binding})}, ?{@vars::@p({type = !gpm.variable_binding})}, ?{@vars::@o({type = !gpm.variable_binding})})
        // expected-error @+1 {{anchor must be a bound variable reference or an identifier}}
        %2 = gpm.relalg.node_expansion %1 @graphs::@g(?{@vars::@s({type = !gpm.variable_binding})} -> ?{@edges::@e({type = !gpm.variable_binding})}) anchor: outgoing
        return
    }
}

// -----

module {
    func.func @main() {
        %0 = gpm.relalg.named_graph column : @graphs::@g({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        %1 = gpm.relalg.triple_pattern %0 @graphs::@g(?{@vars::@y({type = !gpm.variable_binding})}, ?{@vars::@p({type = !gpm.variable_binding})}, ?{@vars::@o({type = !gpm.variable_binding})})
        // expected-error @+1 {{anchor must be a bound variable reference or an identifier}}
        %2 = gpm.relalg.node_expansion %1 @graphs::@g(_{"b0"} -> ?{@edges::@e({type = !gpm.variable_binding})}) anchor: outgoing
        return
    }
}

// -----

module {
    func.func @main() {
        %0 = gpm.relalg.named_graph column : @graphs::@g({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        %1 = gpm.relalg.triple_pattern %0 @graphs::@g(?{@vars::@y({type = !gpm.variable_binding})}, ?{@vars::@p({type = !gpm.variable_binding})}, ?{@vars::@o({type = !gpm.variable_binding})})
        // expected-error @+1 {{target must be a fresh variable definition}}
        %2 = gpm.relalg.node_expansion %1 @graphs::@g(?{@vars::@y} -> ?{@vars::@o}) anchor: outgoing
        return
    }
}

// -----

module {
    func.func @main() {
        %0 = gpm.relalg.named_graph column : @graphs::@g({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        %1 = gpm.relalg.triple_pattern %0 @graphs::@g(?{@vars::@y({type = !gpm.variable_binding})}, ?{@vars::@p({type = !gpm.variable_binding})}, ?{@vars::@o({type = !gpm.variable_binding})})
        // expected-error @+1 {{target must be a fresh variable definition}}
        %2 = gpm.relalg.node_expansion %1 @graphs::@g(?{@vars::@y} -> id{"http://ex.org/foo"}) anchor: outgoing
        return
    }
}
