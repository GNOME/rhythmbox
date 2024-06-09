#!/bin/sh

PROJECT_ID=1857
CI_JOB=build
BRANCH=master

CSS="style.css"
HTML="index.html plugins.html"
ICONS="org.gnome.Rhythmbox3.svg org.gnome.Rhythmbox3-symbolic.svg"
ICONDIR=data/icons/hicolor/scalable/apps

RAWURL="https://gitlab.gnome.org/GNOME/rhythmbox/-/raw/master/"

mkdir -p public
cp $CSS public
cd public

curl --output NEWS $RAWURL/NEWS

for i in $ICONS; do
  curl --output $i $RAWURL/$ICONDIR/$i
done

python3 ../pages.py .. NEWS ../third-party-plugins $HTML

# get last tagged version; no docs from 3.4.7 or before
LATEST_TAG=$( git show-ref --tags | tail -1 | sed 's/^.*\///' )
echo "latest tag $LATEST_TAG"
echo "but there are no releases with docs yet, using $BRANCH"
LATEST_TAG=$BRANCH

curl --output apidoc.zip https://gitlab.gnome.org/api/v4/projects/$PROJECT_ID/jobs/artifacts/$LATEST_TAG/download?job=$CI_JOB
unzip apidoc.zip
mv _build/doc/apidoc .
rm -r _build
rm apidoc.zip
