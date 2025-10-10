from src import demo


def test_demo_runs():
    # basic smoke test to ensure main runs without error
    demo.main()
    assert True
