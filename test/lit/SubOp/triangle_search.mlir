// RUN: env LINGODB_EXECUTION_MODE=DEFAULT run-mlir %s | FileCheck %s
// RUN: %if baseline-backend %{LINGODB_EXECUTION_MODE=BASELINE run-mlir %s | FileCheck %s %}

// Triangle search on a directed graph
//   0 -> 2      1 -> 0      1 -> 2
//   1 -> 4      2 -> 4      2 -> 3
//
// Triangles: (1,0,2) via 1->0, 0->2, 1->2
//            (1,2,4) via 1->2, 2->4, 1->4

//CHECK: |                             a  |                             b  |                             c  |
//CHECK: ----------------------------------------------------------------------------------------------------
//CHECK: |                             1  |                             0  |                             2  |
//CHECK: |                             1  |                             2  |                             4  |
module{
    func.func @main(){
        %subop_result = subop.execution_group (){
        %edges = subop.create !subop.buffer<[edgeFrom : i32, edgeTo : i32]>
        %edgeData, %streams:6 = subop.generate [@c::@from({type=i32}),@c::@to({type=i32})] {
            %c0 = db.constant(0) : i32
            %c1 = db.constant(1) : i32
            %c2 = db.constant(2) : i32
            %c3 = db.constant(3) : i32
            %c4 = db.constant(4) : i32
            subop.generate_emit %c0, %c2 : i32,i32
            subop.generate_emit %c1, %c0 : i32,i32
            subop.generate_emit %c1, %c2 : i32,i32
            subop.generate_emit %c1, %c4 : i32,i32
            subop.generate_emit %c2, %c4 : i32,i32
            subop.generate_emit %c2, %c3 : i32,i32
            tuples.return
        }
        subop.materialize %edgeData {@c::@from=>edgeFrom, @c::@to => edgeTo}, %edges : !subop.buffer<[edgeFrom : i32, edgeTo : i32]>

        // adjacency: from -> {to,...}, used to hop from b to a candidate c
        %adjacency = subop.create !subop.multimap<[from:i32],[to:i32]>
        %buildAdj = subop.scan %edges : !subop.buffer<[edgeFrom : i32, edgeTo : i32]> {edgeFrom => @adjIn::@from({type=i32}), edgeTo => @adjIn::@to({type=i32})}
        subop.insert %buildAdj %adjacency : !subop.multimap<[from:i32],[to:i32]> {@adjIn::@from => from, @adjIn::@to => to}
            eq: ([%l], [%r]){
                %eq = arith.cmpi eq, %l, %r : i32
                tuples.return %eq : i1
            }

        // edgeSet: (from,to) -> exists, used to check the closing edge a->c
        %edgeSet = subop.create !subop.hashmap<[eFrom:i32,eTo:i32],[marker:i8]>
        %buildSet = subop.scan %edges : !subop.buffer<[edgeFrom : i32, edgeTo : i32]> {edgeFrom => @setIn::@from({type=i32}), edgeTo => @setIn::@to({type=i32})}
        %insertedSet = subop.lookup_or_insert %buildSet %edgeSet[@setIn::@from,@setIn::@to] : !subop.hashmap<[eFrom:i32,eTo:i32],[marker:i8]> @setIn::@ref({type=!subop.lookup_entry_ref<!subop.hashmap<[eFrom:i32,eTo:i32],[marker:i8]>>})
            eq: ([%l1,%l2],[%r1,%r2]){
                %eq1 = arith.cmpi eq, %l1, %r1 : i32
                %eq2 = arith.cmpi eq, %l2, %r2 : i32
                %eq = arith.andi %eq1, %eq2 : i1
                tuples.return %eq : i1
            }
            initial: {
                %c1 = arith.constant 1 : i8
                tuples.return %c1 : i8
            }

        // hop 1: scan a->b edges
        %ab = subop.scan %edges : !subop.buffer<[edgeFrom : i32, edgeTo : i32]> {edgeFrom => @ab::@a({type=i32}), edgeTo => @ab::@b({type=i32})}
        // hop 2: b -> c via the adjacency multimap (one-to-many join)
        %hop2 = subop.lookup %ab %adjacency[@ab::@b] : !subop.multimap<[from:i32],[to:i32]> @ab::@list({type=!subop.list<!subop.multi_map_entry_ref<!subop.multimap<[from:i32],[to:i32]>>>})
            eq: ([%l], [%r]){
                %eq = arith.cmpi eq, %l, %r : i32
                tuples.return %eq : i1
            }
        %triples = subop.nested_map %hop2 [@ab::@list] (%tuple, %list){
            %scanned = subop.scan_list %list : !subop.list<!subop.multi_map_entry_ref<!subop.multimap<[from:i32],[to:i32]>>> @bc::@entry({type=!subop.multi_map_entry_ref<!subop.multimap<[from:i32],[to:i32]>>})
            %gathered = subop.gather %scanned @bc::@entry {to => @bc::@c({type=i32})}
            %combined = subop.combine_tuple %gathered, %tuple
            tuples.return %combined : !tuples.tuplestream
        }

        // close the triangle: does a->c exist? (semi-join against edgeSet)
        %closeLookup = subop.lookup %triples %edgeSet[@ab::@a,@bc::@c] : !subop.hashmap<[eFrom:i32,eTo:i32],[marker:i8]> @close::@optref({type=!subop.optional<!subop.lookup_entry_ref<!subop.hashmap<[eFrom:i32,eTo:i32],[marker:i8]>>>})
            eq: ([%l1,%l2],[%r1,%r2]){
                %eq1 = arith.cmpi eq, %l1, %r1 : i32
                %eq2 = arith.cmpi eq, %l2, %r2 : i32
                %eq = arith.andi %eq1, %eq2 : i1
                tuples.return %eq : i1
            }
        %closed = subop.unwrap_optional_ref %closeLookup @close::@optref @close::@ref({type=!subop.lookup_entry_ref<!subop.hashmap<[eFrom:i32,eTo:i32],[marker:i8]>>})

        %result_table = subop.create !subop.result_table<[a0:i32, b0:i32, c0:i32]>
        subop.materialize %closed {@ab::@a => a0, @ab::@b => b0, @bc::@c => c0}, %result_table : !subop.result_table<[a0:i32,b0:i32,c0:i32]>
        %local_table = subop.create_from ["a","b","c"] %result_table : !subop.result_table<[a0:i32,b0:i32,c0:i32]> -> !subop.local_table<[a0:i32,b0:i32,c0:i32],["a","b","c"]>
        subop.execution_group_return %local_table : !subop.local_table<[a0:i32,b0:i32,c0:i32],["a","b","c"]>
        } -> !subop.local_table<[a0:i32,b0:i32,c0:i32],["a","b","c"]>
        subop.set_result 0 %subop_result  : !subop.local_table<[a0:i32,b0:i32,c0:i32],["a","b","c"]>
        return
    }
}
