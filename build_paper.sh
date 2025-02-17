#!/usr/bin/env bash

# Create plots
cd paper
mkdir fig
pipenv run python visualization.py

# Create paper
make all
