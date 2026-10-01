#include "doctest.h"

#include "SurroundTestState.h"

TEST_CASE("Surround Wizard state validates requests and reports compact status")
{
  SurroundTestState state;
  CHECK_FALSE(state.Active());
  CHECK(state.ToCompactString() == "test_state=idle");

  CHECK(state.Start(SurroundTestRoute::FL, 80, -30.0));
  CHECK(state.Active());
  CHECK(state.Route() == SurroundTestRoute::FL);
  CHECK(state.FrequencyHz() == 80);
  CHECK(state.LevelDb() == doctest::Approx(-30.0));
  CHECK(state.ToCompactString().find("route=fl") != std::string::npos);
  CHECK(state.ToCompactString().find("frequency_hz=80") != std::string::npos);

  state.Stop();
  CHECK_FALSE(state.Active());
  CHECK(state.ToCompactString() == "test_state=idle");

  CHECK_FALSE(state.Start(SurroundTestRoute::Off, 80, -30.0));
  CHECK_FALSE(state.Start(SurroundTestRoute::LFE, 10, -30.0));
  CHECK_FALSE(state.Start(SurroundTestRoute::LFE, 80, -3.0));
}

TEST_CASE("Surround Wizard route parser covers discrete and combined routes")
{
  SurroundTestRoute route = SurroundTestRoute::Off;
  CHECK(ParseSurroundTestRoute("fl", route));
  CHECK(route == SurroundTestRoute::FL);
  CHECK(ParseSurroundTestRoute("center", route));
  CHECK(route == SurroundTestRoute::C);
  CHECK(ParseSurroundTestRoute("lfe", route));
  CHECK(route == SurroundTestRoute::LFE);
  CHECK(ParseSurroundTestRoute("fl+lfe", route));
  CHECK(route == SurroundTestRoute::FL_LFE);
  CHECK_FALSE(ParseSurroundTestRoute("banana", route));
}
