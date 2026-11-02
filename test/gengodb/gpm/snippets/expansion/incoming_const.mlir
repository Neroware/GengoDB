module  {
    func.func @main() {
        %res = subop.execution_group (){
            %0 = gpm.relalg.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
            %1 = gpm.relalg.node_expansion %0 @graphs::@ref(id{"http://example.org/coffee"} -> ?{@edges::@e({type = !gpm.variable_binding})}) anchor: incoming
            %out = gpm.relalg.edge_expansion %1 @graphs::@ref(?{@edges::@e} -> ?{@vars::@s({type = !gpm.variable_binding})}) anchor: from_node
            %res_table = relalg.materialize %out [@edges::@e, @vars::@s] => ["e", "s"] : !subop.local_table<[col1: !db.string, col2: !db.string],["e", "s"]>
            subop.execution_group_return %res_table : !subop.local_table<[col1: !db.string, col2: !db.string],["e", "s"]>
        } -> !subop.local_table<[col1: !db.string, col2: !db.string],["e", "s"]>
        subop.set_result 0 %res : !subop.local_table<[col1: !db.string, col2: !db.string],["e", "s"]>
        return
    }
}
