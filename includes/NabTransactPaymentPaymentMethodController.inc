<?php

/**
 * @file
 * Contains NabTransactPaymentPaymentMethodController.
 *
 * Some part of the codes are copied from commerce_nab_transact module
 * see commerce_nab_transact.module.
 */

/**
 * A NAB Transact payment method controller.
 */
class NabTransactPaymentPaymentMethodController extends PaymentMethodController {
  /**
   * Transaction Type: Standard Payment.
   */
  const STANDARD_PAYMENT = 0;

  /**
   * Transaction Type: Preauth Complete (Advice).
   */
  const PREAUTH_COMPLETE = 11;

  public $controller_data_defaults = array(
    'merchant_id' => '',
    'password' => '',
    'mode' => 'live',
    'txn_type' => self::STANDARD_PAYMENT,
  );

  public $payment_method_configuration_form_elements_callback = 'nab_transact_payment_payment_method_configuration_form_elements';

  public $payment_configuration_form_elements_callback = 'nab_transact_payment_payment_configuration_form_elements';

  public function __construct() {
    $this->title = t('NAB Transact XML API');
    $this->description = t("Payment method using NAB Transact XML API");
  }

  /**
   * Implements PaymentMethodController::validate().
   */
  public function validate(Payment $payment, PaymentMethod $payment_method, $strict) {
  }

  /**
   * Implements PaymentMethodController::execute().
   */
  public function execute(Payment $payment) {
    if (isset($payment->method_data['credit_card'])) {
      $credit_card = $payment->method_data['credit_card'];
      $settings = $payment->method->controller_data;

      if (isset($payment->method_data['purchaseOrderNo'])) {
        $purchaseOrderNo = $payment->method_data['purchaseOrderNo'];
      }
      else {
        $purchaseOrderNo = $payment->context . '_' . $payment->pid;
      }

      $data = array(
        'txnType' => $settings['txn_type'],
        'txnSource' => 23,
        'amount' => round($payment->totalAmount(TRUE) * 100),
        'currency' => 'AUD',
        'purchaseOrderNo' => $purchaseOrderNo,
        'CreditCardInfo' => array(
          'cardNumber' => $credit_card['number'],
          'cvv' => $credit_card['code'],
          'expiryDate' => $credit_card['exp_month'] . '/' . substr($credit_card['exp_year'], 2),
          'cardHolderName' => $credit_card['owner'],
          'recurringFlag' => 'no'
        ),
      );

      $message_id = 1;
      $post_data = $this->xml('Payment', $data, $message_id, $settings);

      $url = 'https://transact.nab.com.au/' . $settings['mode'] . '/xmlapi/payment';

      $ch = curl_init($url);

      curl_setopt($ch, CURLOPT_POST, 1);
      if ($settings['mode'] == 'test') {
        curl_setopt($ch, CURLOPT_SSL_VERIFYPEER, FALSE);
      }
      curl_setopt($ch, CURLOPT_POSTFIELDS, $post_data);
      curl_setopt($ch, CURLOPT_RETURNTRANSFER, TRUE);

      $response = curl_exec($ch);

      if (empty($response)) {
        $curl_msg = curl_error($ch);
      }
      else {
        $info = curl_getinfo($ch);

        if ($info['http_code'] != 200 && $info['http_code'] != 201) {
          $curl_msg = $info['http_code'] . $response;
        }
        else {
          $xml = simplexml_load_string($response);
        }
      }
      curl_close($ch);

      if (isset($xml) && is_object($xml)) {
        $transaction_data = array('pid' => $payment->pid);
        $transaction_data['status_code'] = isset($xml->Status->statusCode) ? $xml->Status->statusCode : '';
        $transaction_data['status_description'] = isset($xml->Status->statusDescription) ? $xml->Status->statusDescription : '';

        // Handle response.
        if (isset($xml->Status->statusCode) && $xml->Status->statusCode != '000') {
          // Transaction failed.
          $payment->setStatus(new PaymentStatusItem(PAYMENT_STATUS_FAILED));
        }
        elseif (isset($xml->Payment->TxnList->Txn)) {
          $transaction_data['response_code'] = isset($xml->Payment->TxnList->Txn->responseCode) ? $xml->Payment->TxnList->Txn->responseCode : '';
          $transaction_data['response_text'] = isset($xml->Payment->TxnList->Txn->responseText) ? $xml->Payment->TxnList->Txn->responseText : '';
          $transaction_data['txn_id'] = isset($xml->Payment->TxnList->Txn->txnID) ? $xml->Payment->TxnList->Txn->txnID : NULL;

          if ($xml->Payment->TxnList->Txn->approved == 'Yes') {
            // Set the transaction status based on the type of this transaction.
            switch ($settings['txn_type']) {
              case self::PREAUTH_COMPLETE:
                // Keep status as pending.
                break;

              case self::STANDARD_PAYMENT:
                $payment->setStatus(new PaymentStatusItem(PAYMENT_STATUS_SUCCESS));
                break;
            }
          }
          else {
            // Transaction failed.
            $payment->setStatus(new PaymentStatusItem(PAYMENT_STATUS_FAILED));
          }
        }
        $this->saveTransaction($transaction_data);
      }
      else {
        // Handle error.
        watchdog('nab_transact_payment', "Curl error: %error Payment ID: %pid", array('%error' => $curl_msg, '%pid' => $payment->pid), WATCHDOG_ERROR);
        $payment->setStatus(new PaymentStatusItem(PAYMENT_STATUS_FAILED));
      }
    }
  }

  /**
   * Saves transaction information.
   *
   * @param array $data
   *   Transaction data.
   *
   * @return int
   *   SAVED_NEW or SAVED_UPDATED.
   */
  public function saveTransaction($data = array()) {
    $data['created'] = time();

    $data_defaults = array(
      'status_code' => '',
      'status_description' => '',
      'txn_id' => NULL,
      'response_code' => '',
      'response_text' => ''
    );
    $data += $data_defaults;

    return drupal_write_record('nab_transact_payment', $data);
  }

  /**
   * Loads transaction information.
   *
   * @param int $pid
   *   Payment id.
   *
   * @return array|false
   *   The transaction data, or FALSE in case of failure.
   */
  public function loadTransaction($pid = 0) {
    // Load payment and reference no for the submission.
    $query = db_select('nab_transact_payment', 'ntp');
    $query->fields('ntp', array('created', 'pid', 'status_code', 'status_description', 'txn_id', 'response_code', 'response_text'))
          ->condition('pid', $pid)
          ->range(0, 1);

    $transaction = $query->execute()->fetchAssoc();
    return $transaction;
  }

  /**
   * Build request XML.
   *
   * @return string
   *   NAB XML ready for request.
   */
  private function xml($type, $data, $message_id, $settings) {
    if ($type !== 'Payment' && $type !== 'Echo') {
      return;
    }

    $xml = '<?xml version="1.0" encoding="UTF-8"?><NABTransactMessage>';

    // Element: MessageInfo.
    $xml .= $this->message_info($message_id);

    // Element: MerchantInfo.
    $xml .= $this->merchant_info($settings);

    // Element: RequestType.
    $xml .= '<RequestType>' . $type . '</RequestType>';

    // Element: Payment.
    if ($type == 'Payment') {
      $xml .= $this->payment_xml($data);
    }

    $xml .= '</NABTransactMessage>';

    return $xml;
  }

  /**
   * Build message info XML.
   *
   * @return string
   *   XML MessageInfo element
   */
  private function message_info($message_id) {
    return '<MessageInfo><messageID>' . substr(md5($message_id), 0, 30)
          . '</messageID><messageTimestamp>' . $this->timestamp()
          . '</messageTimestamp><timeoutValue>60</timeoutValue>'
          . '<apiVersion>xml-4.2</apiVersion></MessageInfo>';
  }

  /**
   * Build merchant info XML.
   *
   * @return string
   *   XML MerchantInfo element
   */
  private function merchant_info($settings) {
    return '<MerchantInfo><merchantID>' . $settings['merchant_id']
          . '</merchantID><password>' . $settings['password']
          . '</password></MerchantInfo>';
  }

  /**
   * Build the XML for the payment element.
   *
   * NAB Transact currently only supports 1 payment at a time, hence the hard
   * coded count and ID.
   *
   * @return string
   *   XML Payment element
   */
  private function payment_xml($data) {
    $xml = '<Payment><TxnList count="1"><Txn ID="1">';

    // Create elements from array.
    foreach ($data as $key => $value) {
      if (is_array($value)) {
        $xml .= '<' . $key . '>';
        // Create elements from nested array.
        foreach ($value as $arr_key => $arr_value) {
          $xml .= '<' . $arr_key . '>' . $arr_value . '</' . $arr_key . '>';
        }
        $xml .= '</' . $key . '>';
      }
      else {
        $xml .= '<' . $key . '>' . $value . '</' . $key . '>';
      }
    }

    $xml .= '</Txn></TxnList></Payment>';

    return $xml;
  }

  /**
   * Timestamp to GMT.
   *
   * @param int $time
   *   Optional unix timestamp.
   *
   * @return string
   *   Date in YdmHis000000+000 (php date format)
   */
  private function timestamp($time = NULL) {
    if (empty($time)) {
      $time = time();
    }
    // Return a formatted GMT timestamp.
    return date('YdmHis000000+000', $time);
  }

}
