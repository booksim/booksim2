#!/usr/intel/bin/python
#
# regression tests for Booksim component workloads
#
from simbench import *
min_version_required('0.65')

ws = Workspace.current
examples_dir = ws.resolve('src/examples')

run = RunSimJobs(sys.argv)

sim_swm_fat = Booksim('swm_fat', k=8, warmup_periods=0, max_samples=1, deadlock_warn_timeout=10000)

bms = [
    # these four should give the same results:
    BookSynth(name='001', injection_process='bernoulli'),
    BookSynth(name='002', injection_process='component(random(bernoulli))', swm_app_run_mode=0),
    BookSynth(name='003', injection_process='component(random(bernoulli),packetize(0,1,999999,1.0))', swm_app_run_mode=0),
    BookSynth(name='004', injection_process='component(random(bernoulli),trace(get,message))', swm_app_run_mode=0),

    # barrier sync
    BookSynth(name='005', injection_process='component(SWM(barrier))', swm_args='{100,10}', roi=1, roi_begin=1111, roi_end=2222),

    # 6 & 7 and 8 & 9 pairs should give the same result
    BookSynth(name='006', injection_process='component(SWM(randperm))',
              swm_app_run_mode=1, use_read_write=1, swm_args='{-n100}', roi=1, roi_begin=1111, roi_end=2222),
    BookSynth(name='007', injection_process='component(SWM(randperm),trace(get,eject))',
              swm_app_run_mode=1, use_read_write=1, swm_args='{-n100}', roi=1, roi_begin=1111, roi_end=2222),
    BookSynth(name='008', injection_process='component(SWM(randperm),packetize(78,64,1500,12.5))',
              swm_app_run_mode=1, use_read_write=1, swm_args='{-n100}', roi=1, roi_begin=1111, roi_end=2222),
    BookSynth(name='009', injection_process='component(SWM(randperm),Mppn(1),packetize(78,64,1500,12.5))',
              swm_app_run_mode=1, use_read_write=1, swm_args='{-n100}', roi=1, roi_begin=1111, roi_end=2222),
    BookSynth(name='010', injection_process='component(SWM(randperm),Mppn(3),packetize(78,64,1500,12.5))',
              swm_app_run_mode=1, use_read_write=1, swm_args='{-n100}', roi=1, roi_begin=1111, roi_end=2222),

    # multi-class. All 3 in each of these triples should give the same result
    BookSynth(name='011', swm_app_run_mode=0, classes=2, injection_process=                  'bernoulli',              injection_rate='{0.1,0.2}', use_read_write='{0,1}', write_fraction=0.3),
    BookSynth(name='012', swm_app_run_mode=0, classes=2, injection_process= 'component(random(bernoulli))',            injection_rate='{0.1,0.2}', use_read_write='{0,1}', write_fraction=0.3),
    BookSynth(name='013', swm_app_run_mode=0, classes=2, injection_process='{component(random(bernoulli))}',           injection_rate='{0.1,0.2}', use_read_write='{0,1}', write_fraction=0.3),
    #
    BookSynth(name='014', swm_app_run_mode=0, classes=2, injection_process='{bernoulli,bernoulli}',                    injection_rate='{0.3,0.5}', use_read_write='{0,1}', write_fraction=0.3),
    BookSynth(name='015', swm_app_run_mode=0, classes=2, injection_process='{component(random(bernoulli)),bernoulli}', injection_rate='{0.3,0.5}', use_read_write='{0,1}', write_fraction=0.3),
    BookSynth(name='016', swm_app_run_mode=0, classes=2, injection_process='{bernoulli,component(random(bernoulli))}', injection_rate='{0.3,0.5}', use_read_write='{0,1}', write_fraction=0.3),
    #
    BookSynth(name='017', swm_app_run_mode=0, classes=2, injection_process='{bernoulli,bernoulli}',                    injection_rate='{0.1,0.2}', use_read_write='{1,1}', write_fraction='{0.3,0.7}'),
    BookSynth(name='018', swm_app_run_mode=0, classes=2, injection_process='{component(random(bernoulli)),bernoulli}', injection_rate='{0.1,0.2}', use_read_write='{1,1}', write_fraction='{0.3,0.7}'),
    BookSynth(name='019', swm_app_run_mode=0, classes=2, injection_process='{bernoulli,component(random(bernoulli))}', injection_rate='{0.1,0.2}', use_read_write='{1,1}', write_fraction='{0.3,0.7}'),
    #
    BookSynth(name='020', swm_app_run_mode=0, classes=2, injection_process='{bernoulli,bernoulli}',                    injection_rate='{0.2,0.3}', use_read_write='{1,1}', write_fraction='{0.5,0.1}'),
    BookSynth(name='021', swm_app_run_mode=0, classes=2, injection_process='{component(random(bernoulli)),bernoulli}', injection_rate='{0.2,0.3}', use_read_write='{1,1}', write_fraction='{0.5,0.1}'),
    BookSynth(name='022', swm_app_run_mode=0, classes=2, injection_process='{bernoulli,component(random(bernoulli))}', injection_rate='{0.2,0.3}', use_read_write='{1,1}', write_fraction='{0.5,0.1}'),

    # the 'random' component should be able to instantiate multiple instances per node
    BookSynth(name='023', injection_process='component(random(bernoulli),Mppn(8))', swm_app_run_mode=0),

    # adding some local latency in both directions
    BookSynth(name='024', injection_process='component(SWM(barrier),latency(20,0))',  swm_args='{100,10}', roi=1, roi_begin=1111, roi_end=2222),
    BookSynth(name='025', injection_process='component(SWM(barrier),latency(0,20))',  swm_args='{100,10}', roi=1, roi_begin=1111, roi_end=2222),
    BookSynth(name='026', injection_process='component(SWM(barrier),latency(10,10))', swm_args='{100,10}', roi=1, roi_begin=1111, roi_end=2222),

    # shortcut local messages
    BookSynth(name='027', injection_process='component(SWM(barrier),Mppn(64),local(600),latency(600,600),packetize(66,18,1480,12.5))',
                          swm_args='{100,10}', roi=1, roi_begin=1111, roi_end=2222),

    # SWM with use_read_write=0 (similar to 005 and 006)
    BookSynth(name='028', injection_process='component(SWM(barrier))',
              swm_app_run_mode=1, use_read_write=0, swm_args='{100,10}', roi=1, roi_begin=1111, roi_end=2222),
    BookSynth(name='029', injection_process='component(SWM(randperm))',
              swm_app_run_mode=1, use_read_write=0, swm_args='{-n100}', roi=1, roi_begin=1111, roi_end=2222),

    # SWM hardware accelerated barrier
    BookSynth(name='030', injection_process='component(SWM(barrier),Mppn(8),collxl())',
              swm_app_run_mode=1, swm_args='{1000,100}', swm_shmem_barrier='hwaccel', swm_msg_overhead=20, roi=1, roi_begin=1111, roi_end=2222, roi_end_count=10),
    BookSynth(name='031', injection_process='component(SWM(barrier),Mppn(8),collxl(),packetize(78,64,1500,12.5))',
              swm_app_run_mode=1, swm_args='{1000,100}', swm_shmem_barrier='hwaccel', swm_msg_overhead=20, roi=1, roi_begin=1111, roi_end=2222, roi_end_count=10),
    BookSynth(name='032', injection_process='component(SWM(barrier),Mppn(8),latency(100,100),collxl(),packetize(78,64,1500,12.5))',
              swm_app_run_mode=1, swm_args='{1000,100}', swm_shmem_barrier='hwaccel', swm_msg_overhead=20, roi=1, roi_begin=1111, roi_end=2222, roi_end_count=10),
    BookSynth(name='033', injection_process='component(SWM(barrier),Mppn(8),local(100),latency(100,100),collxl(),packetize(78,64,1500,12.5))',
              swm_app_run_mode=1, swm_args='{1000,100}', swm_shmem_barrier='hwaccel', swm_msg_overhead=20, roi=1, roi_begin=1111, roi_end=2222, roi_end_count=10),
    BookSynth(name='034', injection_process='component(SWM(barrier),Mppn(8),local(100),latency(100,100),collxl(linear),packetize(78,64,1500,12.5))',
              swm_app_run_mode=1, swm_args='{1000,100}', swm_shmem_barrier='hwaccel', swm_msg_overhead=20, roi=1, roi_begin=1111, roi_end=2222, roi_end_count=10),
    BookSynth(name='035', injection_process='component(SWM(barrier),Mppn(8),local(100),latency(100,100),collxl(all2all),packetize(78,64,1500,12.5))',
              swm_app_run_mode=1, swm_args='{1000,100}', swm_shmem_barrier='hwaccel', swm_msg_overhead=20, roi=1, roi_begin=1111, roi_end=2222, roi_end_count=10),

    # config file for components
    BookSynth(name='036', injection_process='component(%s/swm_barrier_8ppn_eth.ccfg)'%examples_dir, # get component spec from a file
              swm_app_run_mode=1, swm_args='{1000,100}', swm_shmem_barrier='hwaccel', swm_msg_overhead=20, roi=1, roi_begin=1111, roi_end=2222, roi_end_count=10),
    # these pairs should give identical results
    BookSynth(name='037', k=4, use_read_write=0, swm_app_run_mode=1, swm_msg_overhead=20, coalescing_degree=12, sm_rate_ghz=4.0,
                outport_util_threshold=0.2, outport_util_estimator=0.001, swm_args='{-n5000}', roi=1, roi_begin=3333, roi_end=4444,
                injection_process='component(SWM(randperm),Mppn(12),latency(10,10),SMC(switch),packetize(78,64,1500,12.5))' ),
    BookSynth(name='038', k=4, use_read_write=0, swm_app_run_mode=1, swm_msg_overhead=20, coalescing_degree=12, sm_rate_ghz=4.0,
                outport_util_threshold=0.2, outport_util_estimator=0.001, swm_args='{-n5000}', roi=1, roi_begin=3333, roi_end=4444,
                injection_process='component(%s/sm_randperm_12ppn_eth.ccfg)'%examples_dir ),
]

run(experiments=[sim_swm_fat], benchmarks=bms, stats=['Flit BW'], statype='f', rclass=BooksimResults)

## expected results:
#
# Flit BW:
# 001,0.398869
# 002,0.398869
# 003,0.398869
# 004,0.398869
# 005,0.006385
# 006,0.005042
# 007,0.005042
# 008,0.006994
# 009,0.006994
# 010,0.021336
# 011,0.632631
# 012,0.632631
# 013,0.632631
# 014,0.885100
# 015,0.885100
# 016,0.885100
# 017,0.565412
# 018,0.565412
# 019,0.565412
# 020,0.795162
# 021,0.795162
# 022,0.795162
# 023,1.633060
# 024,0.006354
# 025,0.006357
# 026,0.006357
# 027,0.005307
# 028,0.006385
# 029,0.004883
# 030,0.001798
# 031,0.012010
# 032,0.011682
# 033,0.011682
# 034,0.011682
# 035,0.377500
# 036,0.377500
# 037,1.291280
# 038,1.291280
