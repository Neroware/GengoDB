module  {
    func.func @main() {
        %res = subop.execution_group (){
            %0 = gpm.relalg.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
            %out = gpm.relalg.edge_expansion %0 @graphs::@ref(id{"http://example.org/cup"} -> ?{@vars::@x({type = !gpm.variable_binding})}) anchor: to_node
            %res_table = relalg.materialize %out [@vars::@x] => ["x"] : !subop.local_table<[col1: !db.string],["x"]>
            subop.execution_group_return %res_table : !subop.local_table<[col1: !db.string],["x"]>
        } -> !subop.local_table<[col1: !db.string],["x"]>
        subop.set_result 0 %res : !subop.local_table<[col1: !db.string],["x"]>
        return
    }
}
