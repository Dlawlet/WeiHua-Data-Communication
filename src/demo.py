"""Simple demo runner for the project's initial skeleton.
"""

import numpy as np


def main():
    print("WeiHua Data Communication demo")
    # small example: sample random demands and capacities
    rng = np.random.default_rng(0)
    demands = rng.uniform(0.1, 1.0, size=(5,))
    capacities = np.ones(5) * 0.8
    print("demands:", demands)
    print("capacities:", capacities)


if __name__ == "__main__":
    main()
