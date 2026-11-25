"""Open-Meteo weather parse and the Air4Thai station median."""
import unittest
from datetime import datetime, timedelta

import helpers  # noqa: F401  (puts server/ on sys.path)
from s3srv import market


def forecast(cur_time="2026-11-24T17:15", start="2026-11-24T00:00", hours=48, days=6):
    t0 = datetime.strptime(start, "%Y-%m-%dT%H:%M")
    times = [(t0 + timedelta(hours=i)).strftime("%Y-%m-%dT%H:%M") for i in range(hours)]
    d0 = t0.date()
    return {
        "current": {"time": cur_time, "temperature_2m": 33.3, "weather_code": 3},
        "hourly": {"time": times,
                   "temperature_2m": [20.0 + i * 0.5 for i in range(hours)],
                   "weather_code": [i % 4 for i in range(hours)]},
        "daily": {"time": [(d0 + timedelta(days=i)).isoformat() for i in range(days)],
                  "temperature_2m_max": [35.4 + i for i in range(days)],
                  "temperature_2m_min": [25.6 + i for i in range(days)],
                  "weather_code": [61] * days},
    }


class OpenMeteoWeather(unittest.TestCase):
    def test_current(self):
        w = market.parse_openmeteo_weather(forecast())
        self.assertEqual((w["tempC"], w["code"], w["condition"]), (33.3, 3, "Overcast"))
        self.assertEqual((w["high"], w["low"]), (35, 26))

    def test_hourly_starts_at_the_current_hour_and_has_eleven_slots(self):
        hourly = market.parse_openmeteo_weather(forecast())["hourly"]
        self.assertEqual(len(hourly), 11)
        self.assertEqual([h["h"] for h in hourly[:3]], [17, 18, 19])
        self.assertEqual(hourly[0], {"h": 17, "tempC": round(20.0 + 17 * 0.5), "code": 17 % 4})
        self.assertEqual(hourly[-1]["h"], 3)  # wraps past midnight

    def test_hourly_empty_when_the_series_does_not_reach_now(self):
        # No slot >= now: an empty row beats showing the oldest hours.
        doc = forecast(cur_time="2026-11-30T12:00")
        self.assertEqual(market.parse_openmeteo_weather(doc)["hourly"], [])

    def test_hourly_skips_null_temps_and_null_codes_read_minus_one(self):
        doc = forecast()
        doc["hourly"]["temperature_2m"][18] = None
        doc["hourly"]["weather_code"][19] = None
        hourly = market.parse_openmeteo_weather(doc)["hourly"]
        self.assertEqual([h["h"] for h in hourly[:2]], [17, 19])
        self.assertEqual(hourly[1]["code"], -1)
        self.assertEqual(len(hourly), 10)

    def test_daily_five_rows_sunday_zero(self):
        daily = market.parse_openmeteo_weather(forecast())["daily"]
        self.assertEqual(len(daily), 5)
        self.assertEqual(daily[0], {"wd": 2, "high": 35, "low": 26, "code": 61})  # Tue
        self.assertEqual([d["wd"] for d in daily], [2, 3, 4, 5, 6])

    def test_daily_missing_values(self):
        doc = forecast(days=2)
        doc["daily"]["temperature_2m_max"] = [None]
        w = market.parse_openmeteo_weather(doc)
        self.assertIsNone(w["high"])
        self.assertEqual(len(w["daily"]), 2)
        self.assertEqual(w["daily"][1], {"wd": 3, "high": None, "low": 27, "code": 61})

    def test_no_current_temperature_is_none(self):
        doc = forecast()
        doc["current"]["temperature_2m"] = None
        self.assertIsNone(market.parse_openmeteo_weather(doc))
        self.assertIsNone(market.parse_openmeteo_weather({}))

    def test_missing_series_still_gives_a_payload(self):
        w = market.parse_openmeteo_weather({"current": {"temperature_2m": 30}})
        self.assertEqual((w["code"], w["hourly"], w["daily"]), (-1, [], []))


class Air4Thai(unittest.TestCase):
    NOW = datetime(2026, 11, 24, 17, 30, tzinfo=market.BANGKOK_TZ)

    def station(self, pm25, lat=13.75, lon=100.50, when="2026-11-24 17:00"):
        date, time_ = when.split()
        return {"lat": str(lat), "long": str(lon),
                "AQILast": {"date": date, "time": time_, "PM25": {"value": str(pm25)},
                            "PM10": {"value": "-1"}, "O3": {"value": "-1"}}}

    def test_median_of_fresh_near_stations(self):
        doc = {"stations": [self.station(9.0), self.station(28.0), self.station(35.4),
                            self.station(200, lat=18.8),                   # Chiang Mai: too far
                            self.station(200, when="2026-11-24 10:00")]}   # stale
        self.assertEqual(market.parse_air4thai(doc, self.NOW), {"aqi": 86, "source": "air4thai"})

    def test_too_few_stations_is_none(self):
        doc = {"stations": [self.station(9.0), self.station(28.0)]}
        self.assertIsNone(market.parse_air4thai(doc, self.NOW))


if __name__ == "__main__":
    unittest.main()
