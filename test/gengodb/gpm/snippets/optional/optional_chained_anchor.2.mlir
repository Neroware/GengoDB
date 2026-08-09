module  {
    func.func @main() {
        %res = subop.execution_group (){
            %0 = gpm.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
            %bgp = gpm.basic_graph_pattern %0 (%arg : !tuples.tuplestream){
                %1 = gpm.triple_pattern %arg @graphs::@ref(?{@vars::@person({type = !gpm.variable_binding})}, id{"http://example.org/eats"}, ?{@vars::@food({type = !gpm.variable_binding})})
                tuples.return %1 : !tuples.tuplestream
            }
            %opt1 = gpm.optional_graph_pattern %bgp (%arg1 : !tuples.tuplestream){
                %2 = gpm.triple_pattern %arg1 @graphs::@ref(?{@vars::@person}, id{"http://example.org/cup"}, ?{@vars::@cup({type = !gpm.variable_binding})})
                tuples.return %2 : !tuples.tuplestream
            }
            %opt2 = gpm.basic_graph_pattern %opt1 (%arg2 : !tuples.tuplestream){
                %3 = gpm.triple_pattern %arg2 @graphs::@ref(?{@vars::@cup}, id{"http://example.org/drinks"}, ?{@vars::@drink({type = !gpm.variable_binding})})
                tuples.return %3 : !tuples.tuplestream
            }
            %res_table = relalg.materialize %opt2 [@vars::@person, @vars::@food, @vars::@cup, @vars::@drink] => ["person", "food", "cup", "drink"] : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string, col4: !db.string],["person", "food", "cup", "drink"]>
            subop.execution_group_return %res_table : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string, col4: !db.string],["person", "food", "cup", "drink"]>
        } -> !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string, col4: !db.string],["person", "food", "cup", "drink"]>
        subop.set_result 0 %res : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string, col4: !db.string],["person", "food", "cup", "drink"]>
        return
    }
}
