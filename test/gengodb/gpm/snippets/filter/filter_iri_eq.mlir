module  {
    func.func @main() {
        %res = subop.execution_group (){
            %0 = gpm.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
            %bgp = gpm.basic_graph_pattern %0 (%arg : !tuples.tuplestream){
                %1 = gpm.triple_pattern %arg @graphs::@ref(id{"http://example.org/bob"}, id{"http://example.org/drinks"}, ?{@vars::@drink({type = !gpm.variable_binding})})
                tuples.return %1 : !tuples.tuplestream
            }
            %filtered = relalg.selection %bgp (%arg0 : !tuples.tuple){
                %drinkVal = tuples.getcol %arg0 @vars::@drink : !gpm.variable_binding
                %drinkVar = gpm.get_binding %drinkVal -> !variant.variant
                %iriStr = db.constant ("http://example.org/coffee") : !db.string
                %iriVar = variant.create_scalar %iriStr : !db.string {type = 1002}
                %pred = variant.cmp eq %iriVar, %drinkVar -> !db.nullable<i1>
                tuples.return %pred : !db.nullable<i1>
            }
            %res_table = relalg.materialize %filtered [@vars::@drink] => ["drink"] : !subop.local_table<[col1: !db.string],["drink"]>
            subop.execution_group_return %res_table : !subop.local_table<[col1: !db.string],["drink"]>
        } -> !subop.local_table<[col1: !db.string],["drink"]>
        subop.set_result 0 %res : !subop.local_table<[col1: !db.string],["drink"]>
        return
    }
}
