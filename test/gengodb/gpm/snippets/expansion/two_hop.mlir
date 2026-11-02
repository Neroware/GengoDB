module  {
    func.func @main() {
        %res = subop.execution_group (){
            %0 = gpm.relalg.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
            %1 = gpm.relalg.node_expansion %0 @graphs::@ref(id{"http://example.org/bob"} -> ?{@edges::@e1({type = !gpm.variable_binding})}) anchor: outgoing
            %2 = gpm.relalg.edge_expansion %1 @graphs::@ref(?{@edges::@e1} -> ?{@vars::@x({type = !gpm.variable_binding})}) anchor: to_node
            %3 = gpm.relalg.node_expansion %2 @graphs::@ref(?{@vars::@x} -> ?{@edges::@e2({type = !gpm.variable_binding})}) anchor: outgoing
            %out = gpm.relalg.edge_expansion %3 @graphs::@ref(?{@edges::@e2} -> ?{@vars::@y({type = !gpm.variable_binding})}) anchor: to_node
            %res_table = relalg.materialize %out [@vars::@x, @vars::@y] => ["x", "y"] : !subop.local_table<[col1: !db.string, col2: !db.string],["x", "y"]>
            subop.execution_group_return %res_table : !subop.local_table<[col1: !db.string, col2: !db.string],["x", "y"]>
        } -> !subop.local_table<[col1: !db.string, col2: !db.string],["x", "y"]>
        subop.set_result 0 %res : !subop.local_table<[col1: !db.string, col2: !db.string],["x", "y"]>
        return
    }
}
