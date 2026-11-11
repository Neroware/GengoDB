// ex:bob ex:cup* ?z  -> ex:bob, _:cup1 (2 rows)
module  {
    func.func @main() {
        %res = subop.execution_group (){
            %0 = gpm.relalg.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
            %out = gpm.relalg.path_expansion %0 @graphs::@ref(id{"http://example.org/bob"} -> ?{@vars::@z({type = !gpm.variable_binding})}) step: [id{"http://example.org/cup"}] min: 0 anchor: outgoing
            %res_table = relalg.materialize %out [@vars::@z] => ["z"] : !subop.local_table<[col1: !db.string],["z"]>
            subop.execution_group_return %res_table : !subop.local_table<[col1: !db.string],["z"]>
        } -> !subop.local_table<[col1: !db.string],["z"]>
        subop.set_result 0 %res : !subop.local_table<[col1: !db.string],["z"]>
        return
    }
}
