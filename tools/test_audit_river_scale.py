"""Small independent graph/balance fixtures for the diagnostic, not the generator."""
import copy
import unittest

import numpy as np

from audit_river_scale import audit, topology
from probe_river_moisture import probe_node


def fixture():
    def point(row, col, q, regime):
        p = [0.0]*21
        p[0:10] = [row, col, regime, 1, q, 5*q, 2, 1, 1, .5]
        p[12], p[20] = .01, regime
        return p
    doc = dict(res_m=100, width_exponent=.26, depth_exponent=.4,
               cols=['row', 'col', 'level', 'acc_km2', 'q_mean', 'q_bf', 'w_bf_m', 'd_bf_m', 'w_mean_m', 'd_mean_m'],
               segments=[dict(id=7, down=-1, join=-1, basin=0, island=0,
                              pts=[point(1, 1, 3, 1), point(2, 1, 3, 1), point(3, 1, 3, 1)]),
                         dict(id=22, down=7, join=0, basin=0, island=0, pts=[point(0, 0, 1, 0)]),
                         dict(id=13, down=7, join=0, basin=0, island=0, pts=[point(0, 2, 1, 0)])],
               basins=[dict(id=0, index=[1., 1.], event_index=[2., 0.])])
    grids = {key: np.ones((4, 3)) for key in ['rain_mm', 'runoff_mm', 'condense_mm', 'recharge_mm']}
    grids['island_id'] = np.zeros((4, 3), dtype=int)
    return doc, grids


class RiverScaleAuditTests(unittest.TestCase):
    def test_moisture_projection_and_volume_units(self):
        class ConstantGrid:
            @staticmethod
            def grid_interp(field, *args):
                return float(field[0, 0])
        budget = dict(lats=np.array([0., 1.]), lons=np.array([0., 1.]),
                      water_column_mm=np.full((2, 2, 2), 10.),
                      wind_u_ms=np.stack([np.full((2, 2), 2.), np.zeros((2, 2))]),
                      wind_v_ms=np.stack([np.zeros((2, 2)), np.full((2, 2), 2.)]))
        # A 20 x 20 m island: four 10 m cells, not four full-width fronts.
        g = dict(island_id=np.zeros((2, 2)), res_km=.01, year_s=336*86400.,
                 condense_mm=np.full((2, 2), 100.), rain_mm=np.full((2, 2), 500.))
        result = probe_node(ConstantGrid(), budget, g, 0., 0.)
        self.assertEqual(result['projected_main_width_m_by_season'], [20., 20.])
        np.testing.assert_allclose(result['full_column_envelope_m3s_by_season'], [.4, .4])
        self.assertAlmostEqual(result['current_condensation_m3s']*g['year_s'], 40.)

    def test_connectors_and_strahler(self):
        doc, _ = fixture()
        _, down, _, _, weights, order, indegree = topology(doc)
        self.assertEqual(down.tolist(), [1, 2, -1, 0, 0])
        self.assertEqual(order.tolist(), [2, 2, 2, 1, 1])
        self.assertEqual(indegree[0], 2)
        self.assertAlmostEqual(weights.sum(), 200+200*np.sqrt(2))

    def test_daily_transition_requirement_not_annual_water_loss(self):
        doc, grids = fixture()
        result = audit(doc, grids, {})
        self.assertEqual(result['regime_transitions'], 1)
        self.assertEqual(result['transitions_with_unexplained_deficit'], 1)
        # On day 1 the two tributaries carry 4 total, downstream carries 3.
        self.assertEqual(result['worst_daily_deficits'][0]['peak_m3s'], 1)
        self.assertEqual(result['worst_daily_deficits'][0]['days'], 1)
        doc['basins'][0]['event_index'] = [1., 1.]
        self.assertEqual(audit(doc, grids, {})['all_points_with_unexplained_deficit'], 0)

    def test_dry_and_invalid_graph(self):
        doc, grids = fixture()
        doc['basins'][0]['event_index'] = [0., 0.]
        result = audit(doc, grids, {})
        self.assertIsNone(result['by_strahler']['1']['wet_day_width_p10_p50_p90_m'])
        self.assertEqual(result['by_strahler']['1']['annual_median_width_p10_p50_p90_m'], [0, 0, 0])
        cycle = copy.deepcopy(doc)
        cycle['segments'][0].update(down=7, join=0)
        with self.assertRaisesRegex(ValueError, 'cycle'):
            topology(cycle)
        bad_join = copy.deepcopy(doc)
        bad_join['segments'][1]['join'] = 100
        with self.assertRaisesRegex(ValueError, 'junction'):
            topology(bad_join)


if __name__ == '__main__':
    unittest.main()
