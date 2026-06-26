module @querymodule  {
    func.func @query() {
        %0 = gpm.basegraph graph : "file://resources/ttl/coffee.ttl", column : @graphs::@coffee({type = !gpm.graph_ref})
        %bgp0 = gpm.basic_graph_pattern %0 (%arg : !tuples.tuplestream){
            %1 = gpm.triple_pattern %arg @graphs::@coffee(?{@vars::@who({type = !gpm.variable_binding})}, id{"ex:drinks"}, id{"ex:Coffee"})
            %2 = gpm.triple_pattern %1 @graphs::@coffee(?{@vars::@who}, id{"rdf:type"}, id{"ex:Person"})
            tuples.return %2 : !tuples.tuplestream
        }
        %bgp1 = gpm.basic_graph_pattern %bgp0 (%arg : !tuples.tuplestream){
            %3 = gpm.triple_pattern %arg @graphs::@coffee(?{@vars::@who2({type = !gpm.variable_binding})}, id{"ex:drinks"}, id{"ex:Tea"})
            %4 = gpm.triple_pattern %3 @graphs::@coffee(?{@vars::@who2}, id{"rdf:type"}, id{"ex:Person"})
            tuples.return %4 : !tuples.tuplestream
        }
        %join = gpm.join %bgp0, %bgp1
        %res_table = relalg.materialize %join [] => [] : !subop.local_table<[],[]>
        subop.set_result 0 %res_table : !subop.local_table<[],[]>
        return
    }
}

