#!/usr/bin/env bash

# Generates a new certificate and private key.
openssl req -new -newkey rsa:4096 -x509 -sha256 -days 365 -nodes -out MyCertificate.crt -keyout MyKey.key
