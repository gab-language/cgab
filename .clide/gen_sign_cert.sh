#!/usr/bin/env bash

# Generates a new certificate and private key.
openssl req -new -newkey rsa:2048 -x509 -sha256 -days 365 -nodes -out MyCertificate.crt -keyout MyKey.key -subj "/CN=localhost"

./vendor/BearSSL/build/brssl ta MyCertificate.crt > vendor/ta2.h
