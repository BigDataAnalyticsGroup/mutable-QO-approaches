#!env python3

import hashlib
import itertools
import math
import multiprocessing
import numpy
import os
import random
import string


random.seed(1)

NUM_TUPLES = 10_000_000
STRLEN = 10
OUTPUT_DIR = os.path.join('benchmark', 'holistic-optimization', 'data')
NUM_DISTINCT_VALUES = NUM_TUPLES // 10
FKEY_JOIN_SELECTIVITY = 1e-8
N_M_JOIN_SELECTIVITY = 1e-6

TYPE_TO_STR = {
    'b':   'BOOL',
    'i8':  'INT(1)',
    'i16': 'INT(2)',
    'i32': 'INT(4)',
    'i64': 'INT(8)',
    'f':   'FLOAT',
    'd':   'DOUBLE',
}

CARDINALITIES = {
    "R_join_order": 1e5,
    "S_join_order": 1e8,
    "T_join_order": 1e7,
    "R_sortedness": 1e8,
    "S_sortedness": 1.1e6,
    "T_sortedness": 1e7,
    "R_fused": 1e5,
    "S_fused": 1e7,
    "T_fused": 1e5,
    "R_validation": 1e3,
    "S_validation": 1e5,
    "T_validation": 1e4,
    "U_validation": 1e2,
}

SCHEMA = {
    "S_join_order": [
        ( 'id',  'i32', ['NOT NULL', 'PRIMARY KEY'] ),
        ( 'val', 'i32', ['NOT NULL'] ),
    ],

    "R_join_order": [
        ( 'id',  'i32', ['NOT NULL', 'PRIMARY KEY'] ),
        ( 'sid', 'i32', ['NOT NULL', 'REFERENCES S_join_order(id)'], {'fkey_join_selectivity': 1, 'num_referenced_tuples': CARDINALITIES['S_join_order']} ),
        ( 'val', 'i32', ['NOT NULL'] ),
    ],

    "T_join_order": [
        ( 'id',  'i32', ['NOT NULL', 'PRIMARY KEY'] ),
        ( 'sid', 'i32', ['NOT NULL', 'REFERENCES S_join_order(id)'], {'fkey_join_selectivity': 1e-11, 'num_referenced_tuples': CARDINALITIES['S_join_order']} ),
        ( 'val', 'i32', ['NOT NULL'] ),
    ],

    # Students
    "R_sortedness": [
        ( 'id',  'i32', ['NOT NULL', 'PRIMARY KEY'] ),
        ( 'val', 'i32', ['NOT NULL'] ),
    ],

    # PhDs
    "S_sortedness": [
        ( 'id',  'i32', ['NOT NULL', 'PRIMARY KEY', 'REFERENCES R_sortedness(id)'], {'fkey_join_selectivity': 1, 'num_referenced_tuples': CARDINALITIES['R_sortedness']} ),
        ( 'val', 'i32', ['NOT NULL'] ),
    ],

    # work for
    "T_sortedness": [
        ( 'rid', 'i32', ['NOT NULL', 'REFERENCES R_sortedness(id)'], {'fkey_join_selectivity': 9.091e-8, 'num_referenced_tuples': CARDINALITIES['S_sortedness']} ),
        ( 'uid', 'i32', ['NOT NULL'] ),
        ( 'val', 'i32', ['NOT NULL'] ),
    ],

    "R_fused": [
        ( 'id',  'i32', ['NOT NULL', 'PRIMARY KEY'] ),
        ( 'val', 'i32', ['NOT NULL'] ),
    ],

    "T_fused": [
        ( 'id',  'i32', ['NOT NULL', 'PRIMARY KEY'] ),
        ( 'val', 'i32', ['NOT NULL'] ),
    ],

    "S_fused": [
        ( 'rid', 'i32', ['NOT NULL', 'REFERENCES R_fused(id)'], {'fkey_join_selectivity': 1, 'num_referenced_tuples': CARDINALITIES['R_fused']} ),
        ( 'tid', 'i32', ['NOT NULL', 'REFERENCES T_fused(id)'], {'fkey_join_selectivity': 1, 'num_referenced_tuples': CARDINALITIES['T_fused']} ),
        ( 'val', 'i32', ['NOT NULL'] ),
    ],

#    "S_validation": [
#        ( 'id',  'i32', ['NOT NULL', 'PRIMARY KEY'] ),
#        ( 'val', 'i32', ['NOT NULL'] ),
#    ],
#
#    "R_validation": [
#        ( 'id',  'i32', ['NOT NULL', 'PRIMARY KEY'] ),
#        ( 'sid', 'i32', ['NOT NULL', 'REFERENCES S_validation(id)'], {'fkey_join_selectivity': 1, 'num_referenced_tuples': CARDINALITIES['S_validation']} ),
#        ( 'val', 'i32', ['NOT NULL'] ),
#    ],
#
#    "U_validation": [
#        ( 'id',  'i32', ['NOT NULL', 'PRIMARY KEY'] ),
#        ( 'val', 'i32', ['NOT NULL'] ),
#    ],
#
#    "T_validation": [
#        ( 'sid', 'i32', ['NOT NULL', 'REFERENCES S_validation(id)'], {'fkey_join_selectivity': 1, 'num_referenced_tuples': CARDINALITIES['S_validation']} ),
#        ( 'uid', 'i32', ['NOT NULL', 'REFERENCES U_validation(id)'], {'fkey_join_selectivity': 1, 'num_referenced_tuples': CARDINALITIES['U_validation']} ),
#        ( 'val', 'i32', ['NOT NULL'] ),
#    ],
}


#=======================================================================================================================
# Helper Functions
#=======================================================================================================================

# Process an `iterable` in groups of size `n`
def grouper(iterable, n):
    it = iter(iterable)
    while True:
        chunk_it = itertools.islice(it, n)
        try:
            first_el = next(chunk_it)
        except StopIteration:
            return
        yield itertools.chain((first_el,), chunk_it)

# Returns a string hash that is consistent across Python invocations.
def get_string_hash(x: str) -> int:
    return int(hashlib.sha256(x.encode('utf-8')).hexdigest(), 16) % 10**8

# Generate `num` distinct integer values, drawn uniformly at random from the range [ `smallest`, `largest` ).
def gen_random_int_values(smallest :int, largest :int, num :int):
    assert largest - smallest >= num

    if largest - smallest == num:
        return list(range(smallest, largest))

    taken = set()
    counter = largest - num
    values = list()

    for i in range(0, num):
        val = random.randrange(smallest, largest - num)
        if val in taken:
            values.append(counter)
            counter += 1
        else:
            taken.add(val)
            values.append(val)

    assert len(values) == len(set(values))
    return values


#=======================================================================================================================
# Data Generation
#=======================================================================================================================

def gen_database(name, schema, path_to_dir):
    with open(os.path.join(path_to_dir, 'schema.sql'), 'w') as sql:
        sql.write(f'''\
CREATE DATABASE {name};
USE {name};
''')

        for table_name, attributes in schema.items():
            sql.write(f'''
CREATE TABLE {table_name}
(
''')
            attrs = list()
            for attr in attributes:
                attrs.append(f'    {attr[0]} {TYPE_TO_STR[attr[1]]} {" ".join(attr[2]) if len(attr) >= 3 else ""}')
            sql.write(',\n'.join(attrs))

            path_to_csv = os.path.join(path_to_dir, f'{table_name}.csv')
            sql.write(f'''
);
'''
)

def gen_column(attr, num_tuples):
    name = attr[0]
    ty = attr[1]
    args = attr[3] if len(attr) >= 4 else dict()
    num_distinct_values = args.get('num_distinct_values', NUM_DISTINCT_VALUES)
    fkey_join_selectivity = args.get('fkey_join_selectivity', FKEY_JOIN_SELECTIVITY)
    n_m_join_selectivity = args.get('n_m_join_selectivity', N_M_JOIN_SELECTIVITY)
    num_referenced_tuples = args.get('num_referenced_tuples', max(NUM_TUPLES, max(CARDINALITIES.values())))
    random.seed(get_string_hash(name))

    if 'id' == name:
        print(f'  + Generated column {name} of {num_tuples:,} rows with keys from 0 to {num_tuples-1:,}.')
        return map(str, range(num_tuples))
    elif 'id' in name: # e.g. fid, rid, etc.
        num_fids_joining = min(int(fkey_join_selectivity * num_referenced_tuples * num_tuples), num_tuples)
        foreign_keys = [ random.randrange(0, num_referenced_tuples) for i in range(num_fids_joining) ]
        foreign_keys.extend([int(num_referenced_tuples)] * (num_tuples - num_fids_joining))
        assert len(foreign_keys) == num_tuples
        random.shuffle(foreign_keys)
        print(f'  + Generated column {name} of {num_tuples:,} rows with {num_fids_joining:,} foreign keys with a join partner.')
        return map(str, foreign_keys)
    elif 'n2m' in name: # n to m join
        num_distinct_values = int(round(1 / n_m_join_selectivity))
        values = gen_random_int_values(-2**31 + 1, 2**31, num_distinct_values)
    elif ty == 'b':
        values = [ 'TRUE', 'FALSE' ]
    elif ty == 'f' or ty == 'd':
        values = [ random.random() for i in range(num_distinct_values) ]
    elif ty == 'i8':
        values = gen_random_int_values( -2**7 + 1,  2**7, min( 2**8 - 1, num_distinct_values))
    elif ty == 'i16':
        values = gen_random_int_values(-2**15 + 1, 2**15, min(2**16 - 1, num_distinct_values))
    elif ty == 'i32':
        values = gen_random_int_values(-2**31 + 1, 2**31, min(2**32 - 1, num_distinct_values))
    elif ty == 'i64':
        values = gen_random_int_values(-2**63 + 1, 2**63, min(2**64 - 1, num_distinct_values))
    else:
        raise Exception('unsupported type')

    data = list(itertools.chain.from_iterable(itertools.repeat(values, math.ceil(num_tuples / len(values)))))[0:num_tuples]
    print(f'  + Generated column {name} of {len(data):,} rows with {len(set(data)):,} distinct values.')
    random.shuffle(data)
    return map(str, data)

def gen_table(table_name, attributes, path_to_dir):
    print(f'Generating data for table {table_name}')
    with multiprocessing.Pool(multiprocessing.cpu_count()) as pool:
        path = os.path.join(path_to_dir, table_name + '.csv')
        num_tuples = CARDINALITIES[table_name] if table_name in CARDINALITIES else NUM_TUPLES
        columns = pool.starmap(gen_column, zip(attributes, [int(num_tuples)] * len(attributes)))

        with open(path, 'w') as csv:
            # write header
            csv.write(','.join(map(lambda attr: attr[0], attributes)) + '\n')
            rows = map(','.join, zip(*columns))
            for g in grouper(rows, 1000):
                csv.write('\n'.join(g))
                csv.write('\n')

def gen_tables(schema, path_to_dir):
    for table_name, attributes in schema.items():
        gen_table(table_name, attributes, path_to_dir)

if __name__ == '__main__':
    print(f'Generating data in {OUTPUT_DIR}')
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    gen_database('holistic_optimization', SCHEMA, OUTPUT_DIR)
    gen_tables(SCHEMA, OUTPUT_DIR)
