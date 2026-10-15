// RUN: mlir-db-opt %s -split-input-file -verify-diagnostics -mlir-print-local-scope | FileCheck %s

// ?y :q ?z -- expand outgoing from bound ?y
module {
    func.func @main() {
        %0 = gpm.relalg.named_graph column : @graphs::@g({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        %1 = gpm.relalg.triple_pattern %0 @graphs::@g(?{@vars::@y({type = !gpm.variable_binding})}, ?{@vars::@p({type = !gpm.variable_binding})}, ?{@vars::@o({type = !gpm.variable_binding})})
        //CHECK: %2 = gpm.relalg.graph_expansion %1 @graphs::@g(?{@vars::@y}, id{"http://ex.org/q"}, ?{@vars::@z({type = !gpm.variable_binding})}) anchor : subject
        %2 = gpm.relalg.graph_expansion %1 @graphs::@g(?{@vars::@y}, id{"http://ex.org/q"}, ?{@vars::@z({type = !gpm.variable_binding})}) anchor: subject
        return
    }
}

// -----

// ?s ?q ?y -- any incoming edge, fresh predicate
module {
    func.func @main() {
        %0 = gpm.relalg.named_graph column : @graphs::@g({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        %1 = gpm.relalg.triple_pattern %0 @graphs::@g(?{@vars::@y({type = !gpm.variable_binding})}, ?{@vars::@p({type = !gpm.variable_binding})}, ?{@vars::@o({type = !gpm.variable_binding})})
        //CHECK: %2 = gpm.relalg.graph_expansion %1 @graphs::@g(?{@vars::@s({type = !gpm.variable_binding})}, ?{@vars::@q({type = !gpm.variable_binding})}, ?{@vars::@y}) anchor : object
        %2 = gpm.relalg.graph_expansion %1 @graphs::@g(?{@vars::@s({type = !gpm.variable_binding})}, ?{@vars::@q({type = !gpm.variable_binding})}, ?{@vars::@y}) anchor: object
        return
    }
}

// -----

// <bob> :q ?z -- constant anchor
module {
    func.func @main() {
        %0 = gpm.relalg.named_graph column : @graphs::@g({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        %1 = gpm.relalg.triple_pattern %0 @graphs::@g(?{@vars::@y({type = !gpm.variable_binding})}, ?{@vars::@p({type = !gpm.variable_binding})}, ?{@vars::@o({type = !gpm.variable_binding})})
        //CHECK: %2 = gpm.relalg.graph_expansion %1 @graphs::@g(id{"http://ex.org/bob"}, id{"http://ex.org/q"}, ?{@vars::@z({type = !gpm.variable_binding})}) anchor : subject
        %2 = gpm.relalg.graph_expansion %1 @graphs::@g(id{"http://ex.org/bob"}, id{"http://ex.org/q"}, ?{@vars::@z({type = !gpm.variable_binding})}) anchor: subject
        return
    }
}

// -----

// ?y :q ?y -- bound self-loop
module {
    func.func @main() {
        %0 = gpm.relalg.named_graph column : @graphs::@g({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        %1 = gpm.relalg.triple_pattern %0 @graphs::@g(?{@vars::@y({type = !gpm.variable_binding})}, ?{@vars::@p({type = !gpm.variable_binding})}, ?{@vars::@o({type = !gpm.variable_binding})})
        //CHECK: %2 = gpm.relalg.graph_expansion %1 @graphs::@g(?{@vars::@y}, id{"http://ex.org/q"}, ?{@vars::@y}) anchor : subject
        %2 = gpm.relalg.graph_expansion %1 @graphs::@g(?{@vars::@y}, id{"http://ex.org/q"}, ?{@vars::@y}) anchor: subject
        return
    }
}

// -----

// ?y (:p|:q) ?z -- predicate list
module {
    func.func @main() {
        %0 = gpm.relalg.named_graph column : @graphs::@g({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        %1 = gpm.relalg.triple_pattern %0 @graphs::@g(?{@vars::@y({type = !gpm.variable_binding})}, ?{@vars::@p({type = !gpm.variable_binding})}, ?{@vars::@o({type = !gpm.variable_binding})})
        //CHECK: %2 = gpm.relalg.graph_expansion %1 @graphs::@g(?{@vars::@y}, [id{"http://ex.org/p"}, id{"http://ex.org/q"}], ?{@vars::@z({type = !gpm.variable_binding})}) anchor : subject
        %2 = gpm.relalg.graph_expansion %1 @graphs::@g(?{@vars::@y}, [id{"http://ex.org/p"}, id{"http://ex.org/q"}], ?{@vars::@z({type = !gpm.variable_binding})}) anchor: subject
        return
    }
}

// -----

// ?s ?p ?o with ?p bound -- predicate anchor
module {
    func.func @main() {
        %0 = gpm.relalg.named_graph column : @graphs::@g({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        %1 = gpm.relalg.triple_pattern %0 @graphs::@g(?{@vars::@y({type = !gpm.variable_binding})}, ?{@vars::@p({type = !gpm.variable_binding})}, ?{@vars::@o({type = !gpm.variable_binding})})
        //CHECK: %2 = gpm.relalg.graph_expansion %1 @graphs::@g(?{@vars::@s({type = !gpm.variable_binding})}, ?{@vars::@p}, ?{@vars::@z({type = !gpm.variable_binding})}) anchor : predicate
        %2 = gpm.relalg.graph_expansion %1 @graphs::@g(?{@vars::@s({type = !gpm.variable_binding})}, ?{@vars::@p}, ?{@vars::@z({type = !gpm.variable_binding})}) anchor: predicate
        return
    }
}

// -----

// one-element predicate list is normalized to a single identifier
module {
    func.func @main() {
        %0 = gpm.relalg.named_graph column : @graphs::@g({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        %1 = gpm.relalg.triple_pattern %0 @graphs::@g(?{@vars::@y({type = !gpm.variable_binding})}, ?{@vars::@p({type = !gpm.variable_binding})}, ?{@vars::@o({type = !gpm.variable_binding})})
        //CHECK: %2 = gpm.relalg.graph_expansion %1 @graphs::@g(?{@vars::@y}, id{"http://ex.org/q"}, ?{@vars::@z({type = !gpm.variable_binding})}) anchor : subject
        %2 = gpm.relalg.graph_expansion %1 @graphs::@g(?{@vars::@y}, [id{"http://ex.org/q"}], ?{@vars::@z({type = !gpm.variable_binding})}) anchor: subject
        return
    }
}

// -----

module {
    func.func @main() {
        %0 = gpm.relalg.named_graph column : @graphs::@g({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        %1 = gpm.relalg.triple_pattern %0 @graphs::@g(?{@vars::@y({type = !gpm.variable_binding})}, ?{@vars::@p({type = !gpm.variable_binding})}, ?{@vars::@o({type = !gpm.variable_binding})})
        // expected-error @+1 {{subject anchor must be a bound variable reference or an identifier}}
        %2 = gpm.relalg.graph_expansion %1 @graphs::@g(?{@vars::@s({type = !gpm.variable_binding})}, id{"http://ex.org/q"}, ?{@vars::@y}) anchor: subject
        return
    }
}

// -----

module {
    func.func @main() {
        %0 = gpm.relalg.named_graph column : @graphs::@g({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        %1 = gpm.relalg.triple_pattern %0 @graphs::@g(?{@vars::@y({type = !gpm.variable_binding})}, ?{@vars::@p({type = !gpm.variable_binding})}, ?{@vars::@o({type = !gpm.variable_binding})})
        // expected-error @+1 {{subject anchor must be a bound variable reference or an identifier}}
        %2 = gpm.relalg.graph_expansion %1 @graphs::@g(_{"b0"}, id{"http://ex.org/q"}, ?{@vars::@y}) anchor: subject
        return
    }
}

// -----

module {
    func.func @main() {
        %0 = gpm.relalg.named_graph column : @graphs::@g({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        %1 = gpm.relalg.triple_pattern %0 @graphs::@g(?{@vars::@y({type = !gpm.variable_binding})}, ?{@vars::@p({type = !gpm.variable_binding})}, ?{@vars::@o({type = !gpm.variable_binding})})
        // expected-error @+1 {{predicate anchor must be a bound variable reference; a constant predicate does not depend on the input relation}}
        %2 = gpm.relalg.graph_expansion %1 @graphs::@g(?{@vars::@y}, id{"http://ex.org/q"}, ?{@vars::@z({type = !gpm.variable_binding})}) anchor: predicate
        return
    }
}

// -----

module {
    func.func @main() {
        %0 = gpm.relalg.named_graph column : @graphs::@g({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        %1 = gpm.relalg.triple_pattern %0 @graphs::@g(?{@vars::@y({type = !gpm.variable_binding})}, ?{@vars::@p({type = !gpm.variable_binding})}, ?{@vars::@o({type = !gpm.variable_binding})})
        // expected-error @+1 {{predicate anchor must be a bound variable reference; a constant predicate does not depend on the input relation}}
        %2 = gpm.relalg.graph_expansion %1 @graphs::@g(?{@vars::@y}, [id{"http://ex.org/p"}, id{"http://ex.org/q"}], ?{@vars::@z({type = !gpm.variable_binding})}) anchor: predicate
        return
    }
}

// -----

module {
    func.func @main() {
        %0 = gpm.relalg.named_graph column : @graphs::@g({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        %1 = gpm.relalg.triple_pattern %0 @graphs::@g(?{@vars::@y({type = !gpm.variable_binding})}, ?{@vars::@p({type = !gpm.variable_binding})}, ?{@vars::@o({type = !gpm.variable_binding})})
        // expected-error @+1 {{predicate list must not be empty}}
        %2 = gpm.relalg.graph_expansion %1 @graphs::@g(?{@vars::@y}, [], ?{@vars::@z({type = !gpm.variable_binding})}) anchor: subject
        return
    }
}

// -----

module {
    func.func @main() {
        %0 = gpm.relalg.named_graph column : @graphs::@g({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        %1 = gpm.relalg.triple_pattern %0 @graphs::@g(?{@vars::@y({type = !gpm.variable_binding})}, ?{@vars::@p({type = !gpm.variable_binding})}, ?{@vars::@o({type = !gpm.variable_binding})})
        // expected-error @+1 {{predicate list must only contain identifiers}}
        %2 = gpm.relalg.graph_expansion %1 @graphs::@g(?{@vars::@y}, [id{"http://ex.org/p"}, ?{@vars::@p}], ?{@vars::@z({type = !gpm.variable_binding})}) anchor: subject
        return
    }
}

// -----

module {
    func.func @main() {
        %0 = gpm.relalg.named_graph column : @graphs::@g({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        %1 = gpm.relalg.triple_pattern %0 @graphs::@g(?{@vars::@y({type = !gpm.variable_binding})}, ?{@vars::@p({type = !gpm.variable_binding})}, ?{@vars::@o({type = !gpm.variable_binding})})
        // expected-error @+1 {{predicate list contains "http://ex.org/p" more than once}}
        %2 = gpm.relalg.graph_expansion %1 @graphs::@g(?{@vars::@y}, [id{"http://ex.org/p"}, id{"http://ex.org/q"}, id{"http://ex.org/p"}], ?{@vars::@z({type = !gpm.variable_binding})}) anchor: subject
        return
    }
}

// -----

module {
    func.func @main() {
        %0 = gpm.relalg.named_graph column : @graphs::@g({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        %1 = gpm.relalg.triple_pattern %0 @graphs::@g(?{@vars::@y({type = !gpm.variable_binding})}, ?{@vars::@p({type = !gpm.variable_binding})}, ?{@vars::@o({type = !gpm.variable_binding})})
        // expected-error @+1 {{predicate cannot be a blank node}}
        %2 = gpm.relalg.graph_expansion %1 @graphs::@g(?{@vars::@y}, _{"b0"}, ?{@vars::@z({type = !gpm.variable_binding})}) anchor: subject
        return
    }
}
