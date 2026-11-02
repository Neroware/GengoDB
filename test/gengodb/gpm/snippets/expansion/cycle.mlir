module  {
    func.func @main() {
        %res = subop.execution_group (){
            %0 = gpm.relalg.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
            %1 = gpm.relalg.triple_pattern %0 @graphs::@ref(?{@vars::@a({type = !gpm.variable_binding})}, ?{@vars::@p({type = !gpm.variable_binding})}, ?{@vars::@b({type = !gpm.variable_binding})})
            %2 = gpm.relalg.node_expansion %1 @graphs::@ref(?{@vars::@a} -> ?{@edges::@e({type = !gpm.variable_binding})}) anchor: outgoing
            %3 = gpm.relalg.edge_expansion %2 @graphs::@ref(?{@edges::@e} -> ?{@vars::@x({type = !gpm.variable_binding})}) anchor: to_node
            %out = relalg.selection %3 (%t: !tuples.tuple) {
                %l = tuples.getcol %t @vars::@a : !gpm.variable_binding
                %r = tuples.getcol %t @vars::@x : !gpm.variable_binding
                %eq = gpm.bindings_compatible %l : !gpm.variable_binding, %r : !gpm.variable_binding
                tuples.return %eq : i1
            }
            %res_table = relalg.materialize %out [@vars::@a, @vars::@x] => ["a", "x"] : !subop.local_table<[col1: !db.string, col2: !db.string],["a", "x"]>
            subop.execution_group_return %res_table : !subop.local_table<[col1: !db.string, col2: !db.string],["a", "x"]>
        } -> !subop.local_table<[col1: !db.string, col2: !db.string],["a", "x"]>
        subop.set_result 0 %res : !subop.local_table<[col1: !db.string, col2: !db.string],["a", "x"]>
        return
    }
}
