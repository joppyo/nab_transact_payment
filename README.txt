CONTENTS OF THIS FILE
---------------------
 * Introduction
 * Requirements
 * Installation
 * Configuration
 * Maintainers

INTRODUCTION
------------
This module provides NAB Transact (http://www.nab.com.au/business/payments-and-merchants/ecommerce-and-online) integration for the
Payment platform (https://drupal.org/project/payment).
Some of the codes for this modules are taken from Drupal Commerce and Commerce NAB Transact modules

REQUIREMENTS
------------
This module requires the following modules:
 * Payment (https://drupal.org/project/payment)
You should have a NAB Transact account to use this module,
it require the merchant id and transaction password from NAB

INSTALLATION
------------
 * Install as you would normally install a contributed drupal module. See:
   https://drupal.org/documentation/install/modules-themes/modules-7
   for further information.

CONFIGURATION
-------------
After installation a payment method for NAB Transact should be created in the
configuration of the Payment platform.
 * Go to admin/config/services/payment
 * Click on Payment methods
 * Click on Add payment method
 * Click on NAB Transact XML API in the list of available payment method types
 * Fill in a title, merchant id and transaction password for your NAB transact
   account. If you set the mode to test, use the test merchant id and password.
 * Click the Save button

MAINTAINERS
-----------
Current maintainers:
 * Joppy Oey (joppy) - https://www.drupal.org/user/3477749
