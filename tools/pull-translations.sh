#! /bin/sh

# Pull the transifex translation files using the command-line tool.

cd ..
pattern='./app/src/main/res/values-*/strings.xml'

git pull
tx pull -a -f

git add $pattern
git commit $pattern -m "res: Updated translations."
