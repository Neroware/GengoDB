module  {
    func.func @main() {
        %res = subop.execution_group (){
            %0 = gpm.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
            %bgp = gpm.basic_graph_pattern %0 (%arg : !tuples.tuplestream){
                %1 = gpm.triple_pattern %arg @graphs::@ref(id{"http://example.org/bob"}, id{"http://example.org/drinks"}, ?{@vars::@what({type = !gpm.variable_binding})})
                %2 = gpm.triple_pattern %1 @graphs::@ref(id{"http://example.org/bob"}, id{"http://example.org/eats"}, ?{@vars::@what2({type = !gpm.variable_binding})})
                tuples.return %2 : !tuples.tuplestream
            }
            %res_table = relalg.materialize %bgp [@vars::@what, @vars::@what2] => ["drinks", "foods"] : !subop.local_table<[col1: !db.string, col2: !db.string],["drinks", "foods"]>
            subop.execution_group_return %res_table : !subop.local_table<[col1: !db.string, col2: !db.string],["drinks", "foods"]>
        } -> !subop.local_table<[col1: !db.string, col2: !db.string],["drinks", "foods"]>
        subop.set_result 0 %res : !subop.local_table<[col1: !db.string, col2: !db.string],["drinks", "foods"]>
        return
    }
}
