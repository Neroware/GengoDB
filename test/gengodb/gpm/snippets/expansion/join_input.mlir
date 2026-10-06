// expansions below and above binding-compatible joins: the relational optimizer (join ordering,
// pushdown, ...) has to keep the expansion's anchors available
module  {
    func.func @main() {
        %res = subop.execution_group (){
            %g1 = gpm.relalg.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
            %t1 = gpm.relalg.triple_pattern %g1 @graphs::@ref(?{@vars::@who({type = !gpm.variable_binding})}, id{"http://example.org/eats"}, ?{@vars::@food({type = !gpm.variable_binding})})
            %g2 = gpm.relalg.named_graph column : @graphs2::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
            %t2 = gpm.relalg.triple_pattern %g2 @graphs2::@ref(?{@bindings::@s({type = !gpm.variable_binding})}, id{"http://example.org/drinks"}, id{"http://example.org/coffee"})
            %j1 = relalg.join %t1, %t2 (%arg0: !tuples.tuple){
                %a = tuples.getcol %arg0 @vars::@who : !gpm.variable_binding
                %b = tuples.getcol %arg0 @bindings::@s : !gpm.variable_binding
                %c = gpm.bindings_compatible %a : !gpm.variable_binding, %b : !gpm.variable_binding
                tuples.return %c : i1
            } attributes {bindingCompatible = [1 : i8], leftHash = [#tuples.columnref<@vars::@who>], nullsEqual = [0 : i8], rightHash = [#tuples.columnref<@bindings::@s>]}
            %e = gpm.relalg.graph_expansion %j1 expand: [@vars::@age({type = !gpm.variable_binding})] pattern: @graphs::@ref(?{@vars::@who}, id{"http://example.org/age"}, ?{@vars::@age})
            %g3 = gpm.relalg.named_graph column : @graphs3::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
            %t3 = gpm.relalg.triple_pattern %g3 @graphs3::@ref(?{@bindings::@x({type = !gpm.variable_binding})}, id{"http://example.org/awake"}, ?{@vars::@awake({type = !gpm.variable_binding})})
            %j2 = relalg.join %e, %t3 (%arg0: !tuples.tuple){
                %a = tuples.getcol %arg0 @vars::@who : !gpm.variable_binding
                %b = tuples.getcol %arg0 @bindings::@x : !gpm.variable_binding
                %c = gpm.bindings_compatible %a : !gpm.variable_binding, %b : !gpm.variable_binding
                tuples.return %c : i1
            } attributes {bindingCompatible = [1 : i8], leftHash = [#tuples.columnref<@vars::@who>], nullsEqual = [0 : i8], rightHash = [#tuples.columnref<@bindings::@x>]}
            %res_table = relalg.materialize %j2 [@vars::@who, @vars::@food, @vars::@age, @vars::@awake] => ["who", "food", "age", "awake"] : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string, col4: !db.string],["who", "food", "age", "awake"]>
            subop.execution_group_return %res_table : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string, col4: !db.string],["who", "food", "age", "awake"]>
        } -> !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string, col4: !db.string],["who", "food", "age", "awake"]>
        subop.set_result 0 %res : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string, col4: !db.string],["who", "food", "age", "awake"]>
        return
    }
}
