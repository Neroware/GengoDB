// RUN: mlir-db-opt %s -split-input-file -verify-diagnostics -mlir-print-local-scope | FileCheck %s

// subject anchor, new object
module {
    func.func @main() {
        %0 = gpm.relalg.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        %1 = gpm.relalg.triple_pattern %0 @graphs::@ref(?{@vars::@who({type = !gpm.variable_binding})}, id{"http://example.org/drinks"}, ?{@vars::@what({type = !gpm.variable_binding})})
        //CHECK: %2 = gpm.relalg.graph_expansion %1 expand : [@vars::@food({type = !gpm.variable_binding})] pattern : @graphs::@ref(?{@vars::@who}, id{"http://example.org/eats"}, ?{@vars::@food})
        %2 = gpm.relalg.graph_expansion %1 expand: [@vars::@food({type = !gpm.variable_binding})] pattern: @graphs::@ref(?{@vars::@who}, id{"http://example.org/eats"}, ?{@vars::@food})
        return
    }
}

// -----

// object anchor (e.g. a literal), new subject
module {
    func.func @main() {
        %0 = gpm.relalg.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"bsbm", "file://resources/ttl/bsbm/product.ttl#rdf">})
        %1 = gpm.relalg.triple_pattern %0 @graphs::@ref(?{@vars::@x({type = !gpm.variable_binding})}, id{"http://www4.wiwiss.fu-berlin.de/bizer/bsbm/v01/vocabulary/productPropertyNumeric1"}, ?{@vars::@known({type = !gpm.variable_binding})})
        //CHECK: gpm.relalg.graph_expansion %1 expand : [@vars::@def({type = !gpm.variable_binding})] pattern : @graphs::@ref(?{@vars::@def}, id{"http://www4.wiwiss.fu-berlin.de/bizer/bsbm/v01/vocabulary/productPropertyNumeric1"}, ?{@vars::@known})
        %2 = gpm.relalg.graph_expansion %1 expand: [@vars::@def({type = !gpm.variable_binding})] pattern: @graphs::@ref(?{@vars::@def}, id{"http://www4.wiwiss.fu-berlin.de/bizer/bsbm/v01/vocabulary/productPropertyNumeric1"}, ?{@vars::@known})
        return
    }
}

// -----

// several new variables, including the predicate
module {
    func.func @main() {
        %0 = gpm.relalg.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        %1 = gpm.relalg.triple_pattern %0 @graphs::@ref(?{@vars::@who({type = !gpm.variable_binding})}, id{"http://example.org/eats"}, id{"http://example.org/sushi"})
        //CHECK: gpm.relalg.graph_expansion %1 expand : [@vars::@p({type = !gpm.variable_binding}), @vars::@x({type = !gpm.variable_binding})] pattern : @graphs::@ref(?{@vars::@who}, ?{@vars::@p}, ?{@vars::@x})
        %2 = gpm.relalg.graph_expansion %1 expand: [@vars::@p({type = !gpm.variable_binding}), @vars::@x({type = !gpm.variable_binding})] pattern: @graphs::@ref(?{@vars::@who}, ?{@vars::@p}, ?{@vars::@x})
        return
    }
}

// -----

// nullable anchors (OPTIONAL) are valid IR, the lowering does not support them yet
module {
    func.func @main() {
        %0 = gpm.relalg.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        %1 = gpm.relalg.triple_pattern %0 @graphs::@ref(?{@vars::@person({type = !gpm.variable_binding})}, id{"http://example.org/eats"}, ?{@vars::@food({type = !gpm.variable_binding})})
        %2 = gpm.relalg.named_graph column : @graphs2::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        %3 = gpm.relalg.triple_pattern %2 @graphs2::@ref(?{@bindings::@s({type = !gpm.variable_binding})}, id{"http://example.org/cup"}, ?{@vars::@cup({type = !gpm.variable_binding})})
        %4 = relalg.outerjoin %1, %3 (%arg0: !tuples.tuple){
            %6 = tuples.getcol %arg0 @vars::@person : !gpm.variable_binding
            %7 = tuples.getcol %arg0 @bindings::@s : !gpm.variable_binding
            %8 = gpm.bindings_compatible %6 : !gpm.variable_binding, %7 : !gpm.variable_binding
            tuples.return %8 : i1
        } mapping: {@outerjoin::@cup({type = !db.nullable<!gpm.variable_binding>})=[@vars::@cup]}
        //CHECK: gpm.relalg.graph_expansion %{{.*}} expand : [@vars::@x({type = !gpm.variable_binding})] pattern : @graphs::@ref(?{@outerjoin::@cup}, id{"http://example.org/contains"}, ?{@vars::@x})
        %5 = gpm.relalg.graph_expansion %4 expand: [@vars::@x({type = !gpm.variable_binding})] pattern: @graphs::@ref(?{@outerjoin::@cup}, id{"http://example.org/contains"}, ?{@vars::@x})
        return
    }
}

// -----

module {
    func.func @main() {
        %0 = gpm.relalg.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        %1 = gpm.relalg.triple_pattern %0 @graphs::@ref(?{@vars::@who({type = !gpm.variable_binding})}, id{"http://example.org/drinks"}, ?{@vars::@what({type = !gpm.variable_binding})})
        // expected-error @+1 {{pattern must reference at least one anchor column of the input relation}}
        %2 = gpm.relalg.graph_expansion %1 expand: [@vars::@a({type = !gpm.variable_binding}), @vars::@b({type = !gpm.variable_binding})] pattern: @graphs::@ref(?{@vars::@a}, id{"http://example.org/eats"}, ?{@vars::@b})
        return
    }
}

// -----

module {
    func.func @main() {
        %0 = gpm.relalg.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        %1 = gpm.relalg.triple_pattern %0 @graphs::@ref(?{@vars::@who({type = !gpm.variable_binding})}, id{"http://example.org/drinks"}, ?{@vars::@what({type = !gpm.variable_binding})})
        // expected-error @+1 {{must expand at least one new variable}}
        %2 = gpm.relalg.graph_expansion %1 expand: [] pattern: @graphs::@ref(?{@vars::@who}, id{"http://example.org/eats"}, ?{@vars::@what})
        return
    }
}

// -----

module {
    func.func @main() {
        %0 = gpm.relalg.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        %1 = gpm.relalg.triple_pattern %0 @graphs::@ref(?{@vars::@who({type = !gpm.variable_binding})}, id{"http://example.org/drinks"}, ?{@vars::@what({type = !gpm.variable_binding})})
        // expected-error @+1 {{expanded column @vars::@unused is not referenced by the pattern}}
        %2 = gpm.relalg.graph_expansion %1 expand: [@vars::@food({type = !gpm.variable_binding}), @vars::@unused({type = !gpm.variable_binding})] pattern: @graphs::@ref(?{@vars::@who}, id{"http://example.org/eats"}, ?{@vars::@food})
        return
    }
}

// -----

module {
    func.func @main() {
        %0 = gpm.relalg.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        %1 = gpm.relalg.triple_pattern %0 @graphs::@ref(?{@vars::@who({type = !gpm.variable_binding})}, id{"http://example.org/drinks"}, ?{@vars::@what({type = !gpm.variable_binding})})
        // expected-error @+1 {{object must reference a column, new variables are declared in 'expand'}}
        %2 = gpm.relalg.graph_expansion %1 expand: [@vars::@food({type = !gpm.variable_binding})] pattern: @graphs::@ref(?{@vars::@who}, id{"http://example.org/eats"}, ?{@vars::@other({type = !gpm.variable_binding})})
        return
    }
}

// -----

module {
    func.func @main() {
        %0 = gpm.relalg.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
        %1 = gpm.relalg.triple_pattern %0 @graphs::@ref(?{@vars::@who({type = !gpm.variable_binding})}, id{"http://example.org/drinks"}, ?{@vars::@what({type = !gpm.variable_binding})})
        // expected-error @+1 {{object must not be a blank node, use a variable instead}}
        %2 = gpm.relalg.graph_expansion %1 expand: [@vars::@food({type = !gpm.variable_binding})] pattern: @graphs::@ref(?{@vars::@who}, ?{@vars::@food}, _{"b0"})
        return
    }
}
