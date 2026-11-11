// RUN: mlir-db-opt %s -split-input-file -verify-diagnostics -mlir-print-local-scope | FileCheck %s

// p+ from a bound subject
module {
    func.func @main() {
        %0 = gpm.relalg.named_graph column : @graphs::@g({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        %1 = gpm.relalg.triple_pattern %0 @graphs::@g(?{@vars::@y({type = !gpm.variable_binding})}, ?{@vars::@p({type = !gpm.variable_binding})}, ?{@vars::@o({type = !gpm.variable_binding})})
        //CHECK: %2 = gpm.relalg.path_expansion %1 @graphs::@g(?{@vars::@y} -> ?{@vars::@z({type = !gpm.variable_binding})}) step : [id{"http://ex.org/p"}] min : 1 anchor : outgoing
        %2 = gpm.relalg.path_expansion %1 @graphs::@g(?{@vars::@y} -> ?{@vars::@z({type = !gpm.variable_binding})}) step: [id{"http://ex.org/p"}] min: 1 anchor: outgoing
        return
    }
}

// -----

// (p|q)* from a constant object, backwards
module {
    func.func @main() {
        %0 = gpm.relalg.named_graph column : @graphs::@g({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        %1 = gpm.relalg.triple_pattern %0 @graphs::@g(?{@vars::@y({type = !gpm.variable_binding})}, ?{@vars::@p({type = !gpm.variable_binding})}, ?{@vars::@o({type = !gpm.variable_binding})})
        //CHECK: %2 = gpm.relalg.path_expansion %1 @graphs::@g(id{"http://ex.org/foo"} -> ?{@vars::@z({type = !gpm.variable_binding})}) step : [id{"http://ex.org/p"}, id{"http://ex.org/q"}] min : 0 anchor : incoming
        %2 = gpm.relalg.path_expansion %1 @graphs::@g(id{"http://ex.org/foo"} -> ?{@vars::@z({type = !gpm.variable_binding})}) step: [id{"http://ex.org/p"}, id{"http://ex.org/q"}] min: 0 anchor: incoming
        return
    }
}

// -----

// both ends bound: reachability check
module {
    func.func @main() {
        %0 = gpm.relalg.named_graph column : @graphs::@g({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        %1 = gpm.relalg.triple_pattern %0 @graphs::@g(?{@vars::@y({type = !gpm.variable_binding})}, ?{@vars::@p({type = !gpm.variable_binding})}, ?{@vars::@o({type = !gpm.variable_binding})})
        //CHECK: %2 = gpm.relalg.path_expansion %1 @graphs::@g(?{@vars::@y} -> ?{@vars::@o}) step : [id{"http://ex.org/p"}] min : 1 anchor : outgoing
        %2 = gpm.relalg.path_expansion %1 @graphs::@g(?{@vars::@y} -> ?{@vars::@o}) step: [id{"http://ex.org/p"}] min: 1 anchor: outgoing
        return
    }
}

// -----

module {
    func.func @main() {
        %0 = gpm.relalg.named_graph column : @graphs::@g({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        %1 = gpm.relalg.triple_pattern %0 @graphs::@g(?{@vars::@y({type = !gpm.variable_binding})}, ?{@vars::@p({type = !gpm.variable_binding})}, ?{@vars::@o({type = !gpm.variable_binding})})
        // expected-error @+1 {{anchor must be a bound variable reference or an identifier}}
        %2 = gpm.relalg.path_expansion %1 @graphs::@g(?{@vars::@s({type = !gpm.variable_binding})} -> ?{@vars::@z({type = !gpm.variable_binding})}) step: [id{"http://ex.org/p"}] min: 1 anchor: outgoing
        return
    }
}

// -----

module {
    func.func @main() {
        %0 = gpm.relalg.named_graph column : @graphs::@g({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        %1 = gpm.relalg.triple_pattern %0 @graphs::@g(?{@vars::@y({type = !gpm.variable_binding})}, ?{@vars::@p({type = !gpm.variable_binding})}, ?{@vars::@o({type = !gpm.variable_binding})})
        // expected-error @+1 {{target must be a variable (fresh definition or bound reference)}}
        %2 = gpm.relalg.path_expansion %1 @graphs::@g(?{@vars::@y} -> id{"http://ex.org/foo"}) step: [id{"http://ex.org/p"}] min: 1 anchor: outgoing
        return
    }
}

// -----

module {
    func.func @main() {
        %0 = gpm.relalg.named_graph column : @graphs::@g({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        %1 = gpm.relalg.triple_pattern %0 @graphs::@g(?{@vars::@y({type = !gpm.variable_binding})}, ?{@vars::@p({type = !gpm.variable_binding})}, ?{@vars::@o({type = !gpm.variable_binding})})
        // expected-error @+1 {{step predicate list must not be empty}}
        %2 = gpm.relalg.path_expansion %1 @graphs::@g(?{@vars::@y} -> ?{@vars::@z({type = !gpm.variable_binding})}) step: [] min: 1 anchor: outgoing
        return
    }
}

// -----

module {
    func.func @main() {
        %0 = gpm.relalg.named_graph column : @graphs::@g({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        %1 = gpm.relalg.triple_pattern %0 @graphs::@g(?{@vars::@y({type = !gpm.variable_binding})}, ?{@vars::@p({type = !gpm.variable_binding})}, ?{@vars::@o({type = !gpm.variable_binding})})
        // expected-error @+1 {{step predicates must be identifiers}}
        %2 = gpm.relalg.path_expansion %1 @graphs::@g(?{@vars::@y} -> ?{@vars::@z({type = !gpm.variable_binding})}) step: [?{@vars::@p}] min: 1 anchor: outgoing
        return
    }
}

// -----

module {
    func.func @main() {
        %0 = gpm.relalg.named_graph column : @graphs::@g({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        %1 = gpm.relalg.triple_pattern %0 @graphs::@g(?{@vars::@y({type = !gpm.variable_binding})}, ?{@vars::@p({type = !gpm.variable_binding})}, ?{@vars::@o({type = !gpm.variable_binding})})
        // expected-error @+1 {{step predicate list contains duplicates}}
        %2 = gpm.relalg.path_expansion %1 @graphs::@g(?{@vars::@y} -> ?{@vars::@z({type = !gpm.variable_binding})}) step: [id{"http://ex.org/p"}, id{"http://ex.org/p"}] min: 1 anchor: outgoing
        return
    }
}

// -----

module {
    func.func @main() {
        %0 = gpm.relalg.named_graph column : @graphs::@g({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        %1 = gpm.relalg.triple_pattern %0 @graphs::@g(?{@vars::@y({type = !gpm.variable_binding})}, ?{@vars::@p({type = !gpm.variable_binding})}, ?{@vars::@o({type = !gpm.variable_binding})})
        // expected-error @+1 {{min must be 0 (p*) or 1 (p+)}}
        %2 = gpm.relalg.path_expansion %1 @graphs::@g(?{@vars::@y} -> ?{@vars::@z({type = !gpm.variable_binding})}) step: [id{"http://ex.org/p"}] min: 2 anchor: outgoing
        return
    }
}
