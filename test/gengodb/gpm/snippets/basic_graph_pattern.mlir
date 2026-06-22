module  {
    func.func @main() {
        %res = subop.execution_group (){
            %0 = gpm.named_graph name : "file://resources/ttl/coffee.ttl#rdf", column : @graphs::@coffee({type = !gpm.graph_ref})
            %bgp = gpm.basic_graph_pattern %0 (%arg : !tuples.tuplestream){
                // %1 = gpm.triple_pattern %arg @graphs::@coffee(?{@vars::@who({type = !gpm.variable_binding})}, id{"ex:drinks"}, id{"ex:Coffee"})
                // %2 = gpm.triple_pattern %1 @graphs::@coffee(?{@vars::@who}, id{"rdf:type"}, id{"ex:Person"})
                // %3 = gpm.triple_pattern %2 @graphs::@coffee(?{@vars::@who}, id{"ex:drinks"}, _{"somedrink"})
                // tuples.return %3 : !tuples.tuplestream
                %1 = gpm.triple_pattern %arg @graphs::@coffee(id{"ex:Steve"}, id{"ex:drinks"}, ?{@vars::@what({type = !gpm.variable_binding})})
                tuples.return %1 : !tuples.tuplestream
            }
            %res_table = relalg.materialize %bgp [@vars::@what] => ["col1"] : !subop.local_table<[col1: !db.string],["col1"]>
            subop.execution_group_return %res_table : !subop.local_table<[col1: !db.string],["col1"]>
        } -> !subop.local_table<[col1: !db.string],["col1"]>
        subop.set_result 0 %res : !subop.local_table<[col1: !db.string],["col1"]>
        return
    }
}
