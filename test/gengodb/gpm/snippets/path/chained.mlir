// ex:anna (ex:cup|ex:drinks)+ ?y . ?y ex:asksForMore+ ?z  (path feeding a path) -> (ex:coffee, ex:coffee) (1 row)
module  {
    func.func @main() {
        %res = subop.execution_group (){
            %0 = gpm.relalg.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
            %1 = gpm.relalg.path_expansion %0 @graphs::@ref(id{"http://example.org/anna"} -> ?{@vars::@y({type = !gpm.variable_binding})}) step: [id{"http://example.org/cup"}, id{"http://example.org/drinks"}] min: 1 anchor: outgoing
            %out = gpm.relalg.path_expansion %1 @graphs::@ref(?{@vars::@y} -> ?{@vars::@z({type = !gpm.variable_binding})}) step: [id{"http://example.org/asksForMore"}] min: 1 anchor: outgoing
            %res_table = relalg.materialize %out [@vars::@y, @vars::@z] => ["y", "z"] : !subop.local_table<[col1: !db.string, col2: !db.string],["y", "z"]>
            subop.execution_group_return %res_table : !subop.local_table<[col1: !db.string, col2: !db.string],["y", "z"]>
        } -> !subop.local_table<[col1: !db.string, col2: !db.string],["y", "z"]>
        subop.set_result 0 %res : !subop.local_table<[col1: !db.string, col2: !db.string],["y", "z"]>
        return
    }
}
