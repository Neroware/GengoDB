// RUN: mlir-db-opt %s -split-input-file -verify-diagnostics -mlir-print-local-scope | FileCheck %s

// two variable ends, alternative step predicates, p+
//CHECK: gpm.relalg.path %0 @graphs::@g(?{@vars::@x({type = !gpm.variable_binding})}, ?{@vars::@z({type = !gpm.variable_binding})}) step : [id{"http://ex.org/p"}, id{"http://ex.org/q"}] min : 1
module {
    func.func @main() {
        %0 = gpm.relalg.named_graph column : @graphs::@g({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        %1 = gpm.relalg.path %0 @graphs::@g(?{@vars::@x({type = !gpm.variable_binding})}, ?{@vars::@z({type = !gpm.variable_binding})}) step: [id{"http://ex.org/p"}, id{"http://ex.org/q"}] min: 1
        return
    }
}

// -----

// constant subject, p*; the same variable on both ends
//CHECK: gpm.relalg.path %0 @graphs::@g(id{"http://ex.org/a"}, ?{@vars::@z({type = !gpm.variable_binding})}) step : [id{"http://ex.org/p"}] min : 0
//CHECK: gpm.relalg.path %0 @graphs::@g(?{@vars::@x({type = !gpm.variable_binding})}, ?{@vars::@x}) step : [id{"http://ex.org/p"}] min : 1
module {
    func.func @main() {
        %0 = gpm.relalg.named_graph column : @graphs::@g({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        %1 = gpm.relalg.path %0 @graphs::@g(id{"http://ex.org/a"}, ?{@vars::@z({type = !gpm.variable_binding})}) step: [id{"http://ex.org/p"}] min: 0
        %2 = gpm.relalg.path %0 @graphs::@g(?{@vars::@x({type = !gpm.variable_binding})}, ?{@vars::@x}) step: [id{"http://ex.org/p"}] min: 1
        return
    }
}

// -----

module {
    func.func @main() {
        %0 = gpm.relalg.named_graph column : @graphs::@g({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        // expected-error @+1 {{step predicate list must not be empty}}
        %1 = gpm.relalg.path %0 @graphs::@g(?{@vars::@x({type = !gpm.variable_binding})}, ?{@vars::@z({type = !gpm.variable_binding})}) step: [] min: 1
        return
    }
}

// -----

module {
    func.func @main() {
        %0 = gpm.relalg.named_graph column : @graphs::@g({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        // expected-error @+1 {{step predicates must be identifiers}}
        %1 = gpm.relalg.path %0 @graphs::@g(?{@vars::@x({type = !gpm.variable_binding})}, ?{@vars::@z({type = !gpm.variable_binding})}) step: [?{@vars::@p({type = !gpm.variable_binding})}] min: 1
        return
    }
}

// -----

module {
    func.func @main() {
        %0 = gpm.relalg.named_graph column : @graphs::@g({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        // expected-error @+1 {{step predicate list contains duplicates}}
        %1 = gpm.relalg.path %0 @graphs::@g(?{@vars::@x({type = !gpm.variable_binding})}, ?{@vars::@z({type = !gpm.variable_binding})}) step: [id{"http://ex.org/p"}, id{"http://ex.org/p"}] min: 1
        return
    }
}

// -----

module {
    func.func @main() {
        %0 = gpm.relalg.named_graph column : @graphs::@g({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        // expected-error @+1 {{min must be 0 (p*) or 1 (p+)}}
        %1 = gpm.relalg.path %0 @graphs::@g(?{@vars::@x({type = !gpm.variable_binding})}, ?{@vars::@z({type = !gpm.variable_binding})}) step: [id{"http://ex.org/p"}] min: 2
        return
    }
}

// -----

module {
    func.func @main() {
        %0 = gpm.relalg.named_graph column : @graphs::@g({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        // expected-error @+1 {{subject must be a variable or an identifier}}
        %1 = gpm.relalg.path %0 @graphs::@g(_{"b0"}, ?{@vars::@z({type = !gpm.variable_binding})}) step: [id{"http://ex.org/p"}] min: 1
        return
    }
}
