#!/usr/intel/bin/python
#
# regression tests for Booksim small message coalescing (SMC) workloads
#
from simbench import *
min_version_required('0.65')

# set packet_size=20 (Ethernet payload in bytes) for SM Ethernet packetize

exps = [
    Booksim('sm_fat', name='sm_fat_c%d'%c,
        sm_rate_ghz=1.0, k=8, deadlock_warn_timeout=10000, vc_buf_size=1000,
        coalescing_degree=c, packet_size=20, outport_util_estimator=0.001)
    for c in [4, 12]
]
single_c_bms = [
    # single traffic class SM only
    BookSynth(name='001', injection_process='component(random(bernoulli),SMC(switch),packetize(66,18,1500,12.5))',     injection_rate=0.05),
    BookSynth(name='002', injection_process='component(random(bernoulli),SMC(switch),packetize(66,18,1500,12.5))',     injection_rate=0.35),
    BookSynth(name='003', injection_process='component(random(bernoulli),SMC(end),packetize(66,18,1500,12.5))', injection_rate=0.05),
    BookSynth(name='004', injection_process='component(random(bernoulli),SMC(end),packetize(66,18,1500,12.5))', injection_rate=0.35),

    # single traffic class SM only, opportunistic with rate threshold
    BookSynth(name='005', outport_util_threshold=0.2, injection_process='component(random(bernoulli),SMC(switch),packetize(66,18,1500,12.5))',     injection_rate=0.05),
    BookSynth(name='006', outport_util_threshold=0.2, injection_process='component(random(bernoulli),SMC(switch),packetize(66,18,1500,12.5))',     injection_rate=0.35),
    BookSynth(name='007', outport_util_threshold=0.2, injection_process='component(random(bernoulli),SMC(end),packetize(66,18,1500,12.5))', injection_rate=0.05),
    BookSynth(name='008', outport_util_threshold=0.2, injection_process='component(random(bernoulli),SMC(end),packetize(66,18,1500,12.5))', injection_rate=0.35),

    # single traffic class SM only, opportunistic with age threshold

    # single traffic class SMC only, RCDC
    BookSynth(name='009', injection_process='component(random(bernoulli),SMC(switch),packetize(66,18,1500,12.5))', rcu_type='rc_dc', internal_speedup=1.0, injection_rate=0.05),
    BookSynth(name='010', injection_process='component(random(bernoulli),SMC(switch),packetize(66,18,1500,12.5))', rcu_type='rc_dc', internal_speedup=1.0, injection_rate=0.45),
    BookSynth(name='011', injection_process='component(random(bernoulli),SMC(switch),packetize(66,18,1500,12.5))', rcu_type='rc_dc', internal_speedup=3.5, injection_rate=0.45),
    BookSynth(name='012', injection_process='component(random(bernoulli),SMC(switch),packetize(66,18,1500,12.5))', rcu_type='rc_dc', internal_speedup=5.0, injection_rate=0.45),

    # single traffic class SM only, component, ethernet
    ##BookSynth(name='017', injection_process='component(random(bernoulli),sm(0),packetize(66,18,1500,12.5))',packet_size=20,injection_rate=0.05),
    # single traffic class SM only, component, XeLink
    ##BookSynth(name='021', injection_process='component(random(bernoulli),sm(0),packetize(8,0,9999999,8.0))',packet_size=20,injection_rate=0.05),
]
# multi_class packet_size=20(bytes) for sm, pakcet_size=88(flits) for bernoulli.
# FIX! when sm is part of the component.
multi_c_bms = [
    # multi traffic class SM + Bernoulli background. Double quotes to protect {} from the shell!
    BookSynth(name='013', classes=2, packet_size='{20,1024}', injection_process='{{component(random(bernoulli),SMC(switch),packetize(66,18,1500,12.5))},{component(random(bernoulli),packetize(66,18,1500,12.5))}}', injection_rate='{0.05,0.005}'),
    BookSynth(name='014', classes=2, packet_size='{20,1024}', injection_process='{{component(random(bernoulli),SMC(switch),packetize(66,18,1500,12.5))},{component(random(bernoulli),packetize(66,18,1500,12.5))}}', injection_rate='{0.35,0.005}'),
    BookSynth(name='015', classes=2, packet_size='{20,1024}', injection_process='{{component(random(bernoulli),SMC(switch),packetize(66,18,1500,12.5))},{component(random(bernoulli),packetize(66,18,1500,12.5))}}', injection_rate='{0.05,0.020}'),
    BookSynth(name='016', classes=2, packet_size='{20,1024}', injection_process='{{component(random(bernoulli),SMC(switch),packetize(66,18,1500,12.5))},{component(random(bernoulli),packetize(66,18,1500,12.5))}}', injection_rate='{0.35,0.020}'),

    # multi traffic class SM + Bernoullie background, RCDC
    BookSynth(name='017', classes=2, packet_size='{20,1024}', injection_process='{{component(random(bernoulli),SMC(switch),packetize(66,18,1500,12.5))},{component(random(bernoulli),packetize(66,18,1500,12.5))}}', injection_rate='{0.05,0.005}', rcu_type='rc_dc', internal_speedup=3.0),
    BookSynth(name='018', classes=2, packet_size='{1024,20}', injection_process='{{component(random(bernoulli),packetize(66,18,1500,12.5))},{component(random(bernoulli),SMC(switch),packetize(66,18,1500,12.5))}}', injection_rate='{0.005,0.05}', rcu_type='rc_dc', internal_speedup=3.0),
    BookSynth(name='019', classes=2, packet_size='{20,1024}', injection_process='{{component(random(bernoulli),SMC(switch),packetize(66,18,1500,12.5))},{component(random(bernoulli),packetize(66,18,1500,12.5))}}', injection_rate='{0.35,0.020}', rcu_type='rc_dc', internal_speedup=5.0),
    BookSynth(name='020', classes=2, packet_size='{1024,20}', injection_process='{{component(random(bernoulli),packetize(66,18,1500,12.5))},{component(random(bernoulli),SMC(switch),packetize(66,18,1500,12.5))}}', injection_rate='{0.020,0.35}', rcu_type='rc_dc', internal_speedup=5.0),
]

RunSimJobs(sys.argv) (
    experiments=[exps[0]], benchmarks=single_c_bms+multi_c_bms,
    stats=['SM BW','Flit BW','Packet BW'], statype='f', rclass=BooksimResults, myprint='xbs'
)
RunSimJobs(sys.argv) (
    experiments=[exps[1]], benchmarks=single_c_bms, # no multi-class for the c=12 case
    stats=['SM BW','Flit BW','Packet BW'], statype='f', rclass=BooksimResults, myprint='xbs'
)

## sm_packet_dev fdaa103e7ff90ab7407721faf83b888ce6ebd93c results:
#
# sm_fat_c4:
# 001,0.049619,0.148851,0.012405
# 002,0.267221,0.801730,0.066805
# 003,0.049944,0.149837,0.012486
# 004,0.276873,0.830574,0.069218
# 005,0.049779,0.258565,0.032502
# 006,0.276473,0.829360,0.069118
# 007,0.049933,0.208516,0.022703
# 008,0.277519,0.832597,0.069380
# 009,0.049869,0.149617,0.012467
# 010,0.114331,0.342963,0.028583
# 011,0.332117,0.996340,0.083029
# 012,0.332510,0.997548,0.083128
# 013,0.049429,0.445426,0.005061
# 014,0.074448,0.441157,0.005012
# 015,0.031371,0.656526,0.007459
# 016,0.034423,0.652757,0.007418
# 017,0.049794,0.447627,0.005086
# 018,0.050098,0.150301,0.012524
# 019,0.155791,0.512642,0.005827
# 020,0.147750,0.443301,0.036937
# sm_fat_c12:
# 001,0.050153,0.104481,0.004179
# 002,0.349131,0.727509,0.029094
# 003,0.050269,0.104722,0.004189
# 004,0.349206,0.727491,0.029101
# 005,0.051023,0.244636,0.029497
# 006,0.349550,0.728095,0.029129
# 007,0.050011,0.207609,0.022419
# 008,0.349656,0.728444,0.029138
# 009,0.049894,0.103954,0.004158
# 010,0.114081,0.237676,0.009507
# 011,0.397313,0.827742,0.033109
# 012,0.448738,0.934737,0.037395
